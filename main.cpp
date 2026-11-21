#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace {

constexpr wchar_t kMainClass[] = L"LabEquipmentTrackerWindow";
constexpr wchar_t kFormClass[] = L"LabEquipmentTrackerForm";
constexpr UINT kRefreshMessage = WM_APP + 1;
constexpr int kSearchId = 100;
constexpr int kListId = 101;
constexpr int kAddId = 102;
constexpr int kEditId = 103;
constexpr int kDeleteId = 104;
constexpr int kNameId = 201;
constexpr int kCategoryId = 202;
constexpr int kLocationId = 203;
constexpr int kStatusId = 204;
constexpr int kServiceId = 205;
constexpr int kSaveId = 206;
constexpr int kCancelId = 207;

const wchar_t* kStatuses[] = {L"Available", L"In Use", L"Maintenance"};

struct Equipment {
    int id = 0;
    std::wstring name;
    std::wstring category;
    std::wstring location;
    std::wstring status;
    std::wstring lastServiced;
};

struct AppState {
    HWND window = nullptr;
    HWND list = nullptr;
    HWND search = nullptr;
    HWND summary = nullptr;
    HFONT font = nullptr;
    std::filesystem::path dataFile;
    std::vector<Equipment> equipment;
};

struct FormContext {
    AppState* app = nullptr;
    Equipment item;
    bool editing = false;
    HWND name = nullptr;
    HWND category = nullptr;
    HWND location = nullptr;
    HWND status = nullptr;
    HWND lastServiced = nullptr;
};

std::string toUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &result[0], size, nullptr, nullptr);
    result.pop_back();
    return result;
}

std::wstring fromUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, &result[0], size);
    result.pop_back();
    return result;
}

std::wstring csvEscape(const std::wstring& value) {
    std::wstring escaped = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') escaped += L"\"\"";
        else escaped += ch;
    }
    escaped += L'"';
    return escaped;
}

bool parseCsvLine(const std::string& line, std::vector<std::string>& fields) {
    fields.clear();
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (quoted) {
            if (ch == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += ch;
            }
        } else if (ch == ',' && !quoted) {
            fields.push_back(field);
            field.clear();
        } else if (ch == '"' && field.empty()) {
            quoted = true;
        } else {
            field += ch;
        }
    }
    if (quoted) return false;
    fields.push_back(field);
    return true;
}

bool loadInventory(const std::filesystem::path& path, std::vector<Equipment>& items, std::wstring& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (std::filesystem::exists(path)) {
            error = L"Could not open " + path.filename().wstring() + L" for reading.";
            return false;
        }
        return true;
    }

    std::string line;
    std::getline(input, line);
    int lineNumber = 1;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::vector<std::string> fields;
        if (!parseCsvLine(line, fields) || fields.size() != 6) {
            error = L"Invalid CSV data on line " + std::to_wstring(lineNumber) + L".";
            return false;
        }
        try {
            Equipment item;
            item.id = std::stoi(fields[0]);
            item.name = fromUtf8(fields[1]);
            item.category = fromUtf8(fields[2]);
            item.location = fromUtf8(fields[3]);
            item.status = fromUtf8(fields[4]);
            item.lastServiced = fromUtf8(fields[5]);
            if (item.id <= 0 || item.name.empty()) {
                error = L"Invalid equipment record on line " + std::to_wstring(lineNumber) + L".";
                return false;
            }
            items.push_back(std::move(item));
        } catch (const std::exception&) {
            error = L"Invalid equipment ID on line " + std::to_wstring(lineNumber) + L".";
            return false;
        }
    }
    if (input.bad()) {
        error = L"An error occurred while reading the equipment file.";
        return false;
    }
    return true;
}

bool saveInventory(const std::filesystem::path& path, const std::vector<Equipment>& items, std::wstring& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = L"Could not save " + path.filename().wstring() + L". Check that the folder is writable.";
        return false;
    }
    output << "id,name,category,location,status,last_serviced\r\n";
    for (const Equipment& item : items) {
        output << item.id << ','
               << toUtf8(csvEscape(item.name)) << ','
               << toUtf8(csvEscape(item.category)) << ','
               << toUtf8(csvEscape(item.location)) << ','
               << toUtf8(csvEscape(item.status)) << ','
               << toUtf8(csvEscape(item.lastServiced)) << "\r\n";
    }
    if (!output) {
        error = L"An error occurred while writing the equipment file.";
        return false;
    }
    return true;
}

std::wstring controlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, &text[0], length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

void setControlFont(HWND control, HFONT font) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

HWND addControl(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style,
                int x, int y, int width, int height, int id, HFONT font) {
    HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
                                   x, y, width, height, parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(nullptr), nullptr);
    if (font) setControlFont(control, font);
    return control;
}

void refreshList(AppState& app) {
    if (!app.list) return;
    ListView_DeleteAllItems(app.list);
    const std::wstring query = controlText(app.search);
    int available = 0;
    int inUse = 0;
    int maintenance = 0;

    for (const Equipment& item : app.equipment) {
        if (item.status == L"Available") ++available;
        else if (item.status == L"In Use") ++inUse;
        else if (item.status == L"Maintenance") ++maintenance;

        const std::wstring searchable = item.name + L" " + item.category + L" " +
                                        item.location + L" " + item.status;
        if (!query.empty() && std::search(searchable.begin(), searchable.end(),
                                          query.begin(), query.end(),
                                          [](wchar_t a, wchar_t b) {
                                              return towlower(a) == towlower(b);
                                          }) == searchable.end()) {
            continue;
        }

        const int row = ListView_GetItemCount(app.list);
        const std::wstring id = std::to_wstring(item.id);
        LVITEMW listItem{};
        listItem.mask = LVIF_TEXT;
        listItem.iItem = row;
        listItem.pszText = const_cast<wchar_t*>(id.c_str());
        ListView_InsertItem(app.list, &listItem);
        ListView_SetItemText(app.list, row, 1, const_cast<wchar_t*>(item.name.c_str()));
        ListView_SetItemText(app.list, row, 2, const_cast<wchar_t*>(item.category.c_str()));
        ListView_SetItemText(app.list, row, 3, const_cast<wchar_t*>(item.location.c_str()));
        ListView_SetItemText(app.list, row, 4, const_cast<wchar_t*>(item.status.c_str()));
        ListView_SetItemText(app.list, row, 5, const_cast<wchar_t*>(item.lastServiced.c_str()));
    }
    const std::wstring text = L"TOTAL  " + std::to_wstring(app.equipment.size()) +
        L"          AVAILABLE  " + std::to_wstring(available) +
        L"          IN USE  " + std::to_wstring(inUse) +
        L"          MAINTENANCE  " + std::to_wstring(maintenance);
    SetWindowTextW(app.summary, text.c_str());
}

void layoutMain(AppState& app, int width, int height) {
    if (!app.list) return;
    const int margin = 24;
    const int contentWidth = std::max(400, width - margin * 2);
    MoveWindow(GetDlgItem(app.window, 301), margin, 22, contentWidth, 34, TRUE);
    MoveWindow(GetDlgItem(app.window, 302), margin, 58, contentWidth, 24, TRUE);
    MoveWindow(app.summary, margin, 99, contentWidth, 32, TRUE);
    MoveWindow(GetDlgItem(app.window, 303), margin, 148, 260, 28, TRUE);
    MoveWindow(app.search, margin, 181, std::max(150, width - 520), 30, TRUE);
    MoveWindow(GetDlgItem(app.window, kAddId), width - 390, 180, 105, 32, TRUE);
    MoveWindow(GetDlgItem(app.window, kEditId), width - 275, 180, 105, 32, TRUE);
    MoveWindow(GetDlgItem(app.window, kDeleteId), width - 160, 180, 136, 32, TRUE);
    MoveWindow(app.list, margin, 226, contentWidth, std::max(120, height - 250), TRUE);
}

void showEquipmentForm(AppState& app, int selectedIndex);

void deleteSelected(AppState& app) {
    const int row = ListView_GetNextItem(app.list, -1, LVNI_SELECTED);
    if (row < 0) {
        MessageBoxW(app.window, L"Select an equipment item to remove.", L"No item selected", MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t idText[32]{};
    ListView_GetItemText(app.list, row, 0, idText, static_cast<int>(std::size(idText)));
    const int id = static_cast<int>(std::wcstol(idText, nullptr, 10));
    const auto found = std::find_if(app.equipment.begin(), app.equipment.end(),
                                    [id](const Equipment& item) { return item.id == id; });
    if (found == app.equipment.end()) return;

    const std::wstring prompt = L"Remove \"" + found->name + L"\" from the tracker?";
    if (MessageBoxW(app.window, prompt.c_str(), L"Confirm removal", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;

    const size_t index = static_cast<size_t>(found - app.equipment.begin());
    Equipment removed = *found;
    app.equipment.erase(found);
    std::wstring error;
    if (!saveInventory(app.dataFile, app.equipment, error)) {
        app.equipment.insert(app.equipment.begin() + static_cast<std::ptrdiff_t>(index), std::move(removed));
        MessageBoxW(app.window, error.c_str(), L"Could not save changes", MB_OK | MB_ICONERROR);
        return;
    }
    refreshList(app);
}

LRESULT CALLBACK formProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    FormContext* context = reinterpret_cast<FormContext*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        context = static_cast<FormContext*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
    }
    if (!context) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_CREATE: {
        HFONT font = context->app->font;
        const wchar_t* labels[] = {L"Equipment name *", L"Category", L"Location", L"Status", L"Last serviced"};
        const int labelY[] = {24, 91, 158, 225, 292};
        for (int i = 0; i < 5; ++i) {
            addControl(window, L"STATIC", labels[i], 0, 24, labelY[i], 390, 20, 0, font);
        }
        context->name = addControl(window, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 24, 46, 410, 30, kNameId, font);
        context->category = addControl(window, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 24, 113, 410, 30, kCategoryId, font);
        context->location = addControl(window, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 24, 180, 410, 30, kLocationId, font);
        context->status = addControl(window, WC_COMBOBOXW, L"", WS_BORDER | CBS_DROPDOWNLIST | WS_VSCROLL, 24, 247, 410, 220, kStatusId, font);
        for (const wchar_t* status : kStatuses) SendMessageW(context->status, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(status));
        context->lastServiced = addControl(window, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 24, 314, 410, 30, kServiceId, font);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kSaveId) {
            Equipment updated;
            updated.name = controlText(context->name);
            updated.category = controlText(context->category);
            updated.location = controlText(context->location);
            updated.lastServiced = controlText(context->lastServiced);
            const LRESULT selection = SendMessageW(context->status, CB_GETCURSEL, 0, 0);
            if (selection >= 0 && selection < static_cast<LRESULT>(std::size(kStatuses))) {
                updated.status = kStatuses[selection];
            }
            if (updated.name.empty()) {
                MessageBoxW(window, L"Enter an equipment name before saving.", L"Name required", MB_OK | MB_ICONINFORMATION);
                SetFocus(context->name);
                return 0;
            }
            if (updated.status.empty()) {
                MessageBoxW(window, L"Choose a status before saving.", L"Status required", MB_OK | MB_ICONINFORMATION);
                SetFocus(context->status);
                return 0;
            }

            updated.id = context->editing ? context->item.id : 1;
            if (!context->editing) {
                for (const Equipment& item : context->app->equipment) updated.id = std::max(updated.id, item.id + 1);
                context->app->equipment.push_back(updated);
            } else {
                const auto found = std::find_if(context->app->equipment.begin(), context->app->equipment.end(),
                    [context](const Equipment& item) { return item.id == context->item.id; });
                if (found == context->app->equipment.end()) {
                    MessageBoxW(window, L"This equipment record is no longer available.", L"Could not save", MB_OK | MB_ICONERROR);
                    return 0;
                }
                *found = updated;
            }

            std::wstring error;
            if (!saveInventory(context->app->dataFile, context->app->equipment, error)) {
                if (context->editing) {
                    const auto found = std::find_if(context->app->equipment.begin(), context->app->equipment.end(),
                        [context](const Equipment& item) { return item.id == context->item.id; });
                    if (found != context->app->equipment.end()) *found = context->item;
                } else {
                    context->app->equipment.pop_back();
                }
                MessageBoxW(window, error.c_str(), L"Could not save changes", MB_OK | MB_ICONERROR);
                return 0;
            }
            SendMessageW(context->app->window, kRefreshMessage, 0, 0);
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wParam) == kCancelId) {
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void showEquipmentForm(AppState& app, int selectedIndex) {
    FormContext context;
    context.app = &app;
    context.editing = selectedIndex >= 0 && static_cast<size_t>(selectedIndex) < app.equipment.size();
    if (context.editing) context.item = app.equipment[static_cast<size_t>(selectedIndex)];

    const wchar_t* title = context.editing ? L"Edit equipment" : L"Add equipment";
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, kFormClass, title,
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 480, 430,
                                  app.window, nullptr, GetModuleHandleW(nullptr), &context);
    if (!dialog) {
        MessageBoxW(app.window, L"Could not open the equipment form.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    SetWindowTextW(context.name, context.item.name.c_str());
    SetWindowTextW(context.category, context.item.category.c_str());
    SetWindowTextW(context.location, context.item.location.c_str());
    SetWindowTextW(context.lastServiced, context.item.lastServiced.c_str());
    int statusIndex = 0;
    for (int i = 0; i < static_cast<int>(std::size(kStatuses)); ++i) {
        if (context.item.status == kStatuses[i]) statusIndex = i;
    }
    SendMessageW(context.status, CB_SETCURSEL, statusIndex, 0);
    const int buttonY = 360;
    addControl(dialog, L"BUTTON", L"Save", BS_DEFPUSHBUTTON, 254, buttonY, 84, 32, kSaveId, app.font);
    addControl(dialog, L"BUTTON", L"Cancel", 0, 350, buttonY, 84, 32, kCancelId, app.font);
    RECT ownerRect{};
    GetWindowRect(app.window, &ownerRect);
    SetWindowPos(dialog, HWND_TOP,
                 ownerRect.left + ((ownerRect.right - ownerRect.left) - 480) / 2,
                 ownerRect.top + ((ownerRect.bottom - ownerRect.top) - 430) / 2,
                 0, 0, SWP_NOSIZE);
    EnableWindow(app.window, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG message{};
    while (IsWindow(dialog)) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(app.window, TRUE);
    SetForegroundWindow(app.window);
    refreshList(app);
}

LRESULT CALLBACK mainProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<AppState*>(create->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_CREATE: {
        app->font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        addControl(window, L"STATIC", L"Lab Equipment Tracker", SS_LEFT, 24, 22, 400, 34, 301, app->font);
        addControl(window, L"STATIC", L"Keep your lab inventory organized and up to date.", SS_LEFT, 24, 58, 500, 24, 302, app->font);
        app->summary = addControl(window, L"STATIC", L"", SS_LEFT, 24, 99, 700, 32, 304, app->font);
        addControl(window, L"STATIC", L"EQUIPMENT INVENTORY", SS_LEFT, 24, 148, 260, 28, 303, app->font);
        app->search = addControl(window, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, 24, 181, 400, 30, kSearchId, app->font);
        SendMessageW(app->search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search equipment, category, location, or status"));
        addControl(window, L"BUTTON", L"+ Add equipment", BS_PUSHBUTTON, 0, 180, 105, 32, kAddId, app->font);
        addControl(window, L"BUTTON", L"Edit selected", BS_PUSHBUTTON, 0, 180, 105, 32, kEditId, app->font);
        addControl(window, L"BUTTON", L"Delete", BS_PUSHBUTTON, 0, 180, 136, 32, kDeleteId, app->font);
        app->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                   WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                   24, 226, 800, 400, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)),
                                   GetModuleHandleW(nullptr), nullptr);
        setControlFont(app->list, app->font);
        ListView_SetExtendedListViewStyle(app->list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        const wchar_t* headings[] = {L"ID", L"Equipment", L"Category", L"Location", L"Status", L"Last serviced"};
        const int widths[] = {60, 220, 150, 150, 120, 130};
        for (int i = 0; i < 6; ++i) {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            column.pszText = const_cast<wchar_t*>(headings[i]);
            column.cx = widths[i];
            column.iSubItem = i;
            ListView_InsertColumn(app->list, i, &column);
        }
        refreshList(*app);
        return 0;
    }
    case WM_SIZE:
        layoutMain(*app, LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x = 850;
        limits->ptMinTrackSize.y = 560;
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == kAddId) {
            showEquipmentForm(*app, -1);
            return 0;
        }
        if (LOWORD(wParam) == kEditId) {
            const int row = ListView_GetNextItem(app->list, -1, LVNI_SELECTED);
            if (row < 0) {
                MessageBoxW(window, L"Select an equipment item to edit.", L"No item selected", MB_OK | MB_ICONINFORMATION);
                return 0;
            }
            wchar_t idText[32]{};
            ListView_GetItemText(app->list, row, 0, idText, static_cast<int>(std::size(idText)));
            const int id = static_cast<int>(std::wcstol(idText, nullptr, 10));
            const auto found = std::find_if(app->equipment.begin(), app->equipment.end(),
                                            [id](const Equipment& item) { return item.id == id; });
            if (found != app->equipment.end()) showEquipmentForm(*app, static_cast<int>(found - app->equipment.begin()));
            return 0;
        }
        if (LOWORD(wParam) == kDeleteId) {
            deleteSelected(*app);
            return 0;
        }
        if (LOWORD(wParam) == kSearchId && HIWORD(wParam) == EN_CHANGE) {
            refreshList(*app);
            return 0;
        }
        break;
    case WM_NOTIFY:
        if (reinterpret_cast<NMHDR*>(lParam)->idFrom == kListId &&
            reinterpret_cast<NMHDR*>(lParam)->code == NM_DBLCLK) {
            SendMessageW(window, WM_COMMAND, kEditId, 0);
            return 0;
        }
        break;
    case kRefreshMessage:
        refreshList(*app);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool registerWindowClass(HINSTANCE instance, const wchar_t* name, WNDPROC procedure, HBRUSH background) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = procedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = background;
    windowClass.lpszClassName = name;
    return RegisterClassExW(&windowClass) != 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);

    if (!registerWindowClass(instance, kMainClass, mainProcedure, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1)) ||
        !registerWindowClass(instance, kFormClass, formProcedure, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1))) {
        MessageBoxW(nullptr, L"Could not initialize the application windows.", L"Startup error", MB_OK | MB_ICONERROR);
        return 1;
    }

    AppState app;
    app.dataFile = std::filesystem::current_path() / L"lab_equipment.csv";
    std::wstring error;
    if (!loadInventory(app.dataFile, app.equipment, error)) {
        MessageBoxW(nullptr, error.c_str(), L"Could not load inventory", MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND window = CreateWindowExW(0, kMainClass, L"Lab Equipment Tracker",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1120, 720,
                                  nullptr, nullptr, instance, &app);
    if (!window) {
        MessageBoxW(nullptr, L"Could not create the application window.", L"Startup error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(window, showCommand);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}