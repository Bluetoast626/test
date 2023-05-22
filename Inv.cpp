#include <windows.h>
#include <string>
#include <fstream>
#include <vector>
#include <sstream>
#include <unordered_map>
#include <ctime>
#include "rapidcsv.h"
#include <filesystem>
#include <exception>

#define ID_USERSELECTION     1001
#define ID_ACTIONSELECTION   1002
#define ID_QUANTITYSELECTION 1003
#define ID_STARTSCAN         1004
#define ID_ACTIONLOG         1005
#define ID_TIMER             1006
#define ID_BARCODEINPUT 1007


std::string barcodeInput;
UINT_PTR timer = 0;
std::vector<std::string> actionLog;
std::string currentUser;
bool checkIn;
int quantity;

std::wstring s2ws(const std::string& s)
{
    int len;
    int slength = (int)s.length() + 1;
    len = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, s.c_str(), slength, 0, 0);
    wchar_t* buf = new wchar_t[len];
    MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, s.c_str(), slength, buf, len);
    std::wstring r(buf);
    delete[] buf;
    return r;
}



class InventoryManager
{
public:
    InventoryManager(const std::string& filePath);
    void UpdateInventory(const std::string& barcode, int quantity, bool checkIn);
    std::string CheckLowQuantity(const std::string& barcode);

private:
    rapidcsv::Document mDoc;
    std::unordered_map<std::string, int> mBarcodeToIndex;
};

InventoryManager* inventoryManager = nullptr;

InventoryManager::InventoryManager(const std::string& filePath) : mDoc(filePath)
{
    if (!std::filesystem::exists(filePath)) {
        throw std::runtime_error("File not found: " + filePath);
    }

    std::vector<std::string> barcodes = mDoc.GetColumn<std::string>("UPC#");
    for (size_t i = 0; i < barcodes.size(); i++)
    {
        std::stringstream ss(barcodes[i]);
        std::string barcode;
        while (std::getline(ss, barcode, ':'))
        {
            mBarcodeToIndex[barcode] = i;
        }
    }
}

void InventoryManager::UpdateInventory(const std::string& barcode, int quantity, bool checkIn)
{
    auto it = mBarcodeToIndex.find(barcode);
    if (it == mBarcodeToIndex.end())
    {
        throw std::runtime_error("Barcode not found: " + barcode);
    }

    int currQuantity = mDoc.GetCell<int>("Current Quantity", it->second);

    if (checkIn)
        currQuantity += quantity;
    else if (currQuantity >= quantity)
        currQuantity -= quantity;

    mDoc.SetCell("Current Quantity", it->second, currQuantity);
}

std::string InventoryManager::CheckLowQuantity(const std::string& barcode)
{
    auto it = mBarcodeToIndex.find(barcode);
    if (it != mBarcodeToIndex.end())
    {
        int lowQuantity = mDoc.GetCell<int>("Low Quantity", it->second);
        int currQuantity = mDoc.GetCell<int>("Current Quantity", it->second);

        if (currQuantity <= lowQuantity)
            return "WARNING: Item with barcode " + barcode + " is low on stock. Current quantity: " + std::to_string(currQuantity);
    }

    return "";
}

void InitControls(HWND hwnd);

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_CREATE:
        try {
            inventoryManager = new InventoryManager("inventory.csv");
        }
        catch (const std::exception& e) {
            std::string str = e.what();
            std::wstring wstr = s2ws(str);
            MessageBox(NULL, wstr.c_str(), L"Error", MB_OK | MB_ICONERROR);
            PostQuitMessage(0);
        }
        InitControls(hwnd);
        return 0;

    case WM_COMMAND:
    {
        switch (LOWORD(wparam))
        {
        case ID_BARCODEINPUT:
        {
            if (HIWORD(wparam) == EN_CHANGE) // Only handle the EN_CHANGE notification
            {
                HWND hInput = (HWND)lparam; // This is the handle of the Edit Control

                // Get the length of the text in the Edit Control
                int len = SendMessage(hInput, WM_GETTEXTLENGTH, 0, 0);
                char buf[256];
                // Only handle if the last character is the Enter key ('\n')

                if (SendMessage(hInput, EM_GETLINE, len - 1, (LPARAM)buf) == 1 && buf[0] == '\n')
                {
                    // Get the scan data
                    std::string scanData(len + 1, '\0');
                    SendMessage(hInput, WM_GETTEXT, len + 1, (LPARAM)&scanData[0]);

                    // Remove the ending '\n'
                    scanData.pop_back();

                    // Handle the scan data here...
                }
            }
            break;
        }
        case ID_STARTSCAN:
        {
            try {
                char buffer[256] = { 0 }; // Initialize the buffer to zeroes

                int userIndex = SendMessage(GetDlgItem(hwnd, ID_USERSELECTION), CB_GETCURSEL, 0, 0);
                SendMessage(GetDlgItem(hwnd, ID_USERSELECTION), CB_GETLBTEXT, (WPARAM)userIndex, (LPARAM)buffer);
                currentUser = buffer;

                checkIn = SendMessage(GetDlgItem(hwnd, ID_ACTIONSELECTION), BM_GETCHECK, 0, 0) == BST_CHECKED;

                HWND hQuantity = GetDlgItem(hwnd, ID_QUANTITYSELECTION);
                if (!hQuantity) {
                    MessageBox(NULL, L"Quantity window handle not found.", L"Error", MB_OK | MB_ICONERROR);
                    return 0;
                }

                // Get the length of the text box content
                int len = SendMessage(hQuantity, WM_GETTEXTLENGTH, 0, 0);

                // Fetch the text box content into the buffer
                SendMessage(hQuantity, WM_GETTEXT, len + 1, reinterpret_cast<LPARAM>(buffer));
                buffer[len] = '\0'; // Ensure null termination

                // Debug: Display the buffer contents in a message box
                std::wstring debugStr = L"Buffer contents: " + std::wstring(buffer, buffer + len);
                MessageBox(NULL, debugStr.c_str(), L"Debug", MB_OK);

                try {
                    quantity = std::stoi(buffer);
                }
                catch (const std::invalid_argument& ia) {
                    MessageBox(NULL, L"Invalid quantity. Please enter a valid number.", L"Error", MB_OK | MB_ICONERROR);
                    return 0;
                }
                catch (const std::out_of_range& oor) {
                    MessageBox(NULL, L"Quantity out of range. Please enter a smaller number.", L"Error", MB_OK | MB_ICONERROR);
                    return 0;
                }

                inventoryManager->UpdateInventory(barcodeInput, quantity, checkIn);

                std::string action = currentUser + (checkIn ? " checked in " : " checked out ") + std::to_string(quantity) + " of item with barcode " + barcodeInput;
                actionLog.push_back(action);

                std::string warning = inventoryManager->CheckLowQuantity(barcodeInput);
                if (!warning.empty()) {
                    actionLog.push_back(warning);
                }

                std::string actionLogText;
                for (const std::string& a : actionLog)
                {
                    actionLogText += a + "\r\n";
                }

                SendMessage(GetDlgItem(hwnd, ID_ACTIONLOG), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(actionLogText.c_str()));
            }
            catch (const std::exception& e) {
                std::string str = e.what();
                std::wstring wstr = s2ws(str);
                MessageBox(NULL, wstr.c_str(), L"Error", MB_OK | MB_ICONERROR);
            }
            return 0;
        }

        default:
            return DefWindowProc(hwnd, msg, wparam, lparam);
        }

    }

    case WM_DESTROY:
        delete inventoryManager;
        PostQuitMessage(0);
        return 0;

    case WM_TIMER:
        switch (wparam)
        {
        case ID_TIMER:
            KillTimer(hwnd, ID_TIMER);
            barcodeInput = ""; // reset barcode input after timeout
            break;
        }
        return 0;

    case WM_CHAR:
        if (wparam == VK_RETURN)
        {
            KillTimer(hwnd, ID_TIMER);
            SetTimer(hwnd, ID_TIMER, 100, NULL); // set timer for 100 milliseconds
        }
        else
        {
            barcodeInput += (char)wparam;
        }
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }
}

void InitControls(HWND hwnd)
{
    HWND hUserSelection = CreateWindow(L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
        50, 50, 100, 100, hwnd, (HMENU)ID_USERSELECTION, NULL, NULL);
    SendMessage(hUserSelection, CB_ADDSTRING, 0, (LPARAM)L"Lai");
    SendMessage(hUserSelection, CB_ADDSTRING, 0, (LPARAM)L"Arevalo");
    SendMessage(hUserSelection, CB_ADDSTRING, 0, (LPARAM)L"Villapando");



    CreateWindow(L"BUTTON", L"Check In", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        50, 100, 100, 30, hwnd, (HMENU)ID_ACTIONSELECTION, NULL, NULL);

    CreateWindow(L"BUTTON", L"Check Out", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
        50, 130, 100, 30, hwnd, (HMENU)(ID_ACTIONSELECTION + 1), NULL, NULL);

    CreateWindow(L"EDIT", L"1", WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_BORDER,
        50, 160, 100, 30, hwnd, (HMENU)ID_QUANTITYSELECTION, NULL, NULL);

    CreateWindow(L"BUTTON", L"Start Scan", WS_CHILD | WS_VISIBLE,
        50, 200, 100, 30, hwnd, (HMENU)ID_STARTSCAN, NULL, NULL);

    CreateWindow(L"EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | WS_BORDER,
        200, 50, 400, 300, hwnd, (HMENU)ID_ACTIONLOG, NULL, NULL);

    CreateWindow(L"EDIT", NULL, WS_CHILD | WS_BORDER,
        0, 0, 0, 0, hwnd, (HMENU)ID_BARCODEINPUT, NULL, NULL);

    SetFocus(GetDlgItem(hwnd, ID_BARCODEINPUT));
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
    const wchar_t CLASS_NAME[] = L"Sample Window Class";

    WNDCLASS wc = { };

    wc.lpfnWndProc = WindowProcedure;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"Inventory Manager", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL)
    {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
