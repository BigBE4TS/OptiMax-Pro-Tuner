#pragma once

#include <windows.h>
#include <commdlg.h>
#include <string>

namespace OptiMax {
    class FileDialog {
    public:
        // Opens a native Windows file browser and returns just the .exe name
        static std::string SelectExecutable(HWND ownerWindow) {
            OPENFILENAMEW ofn;
            wchar_t szFile[MAX_PATH] = { 0 };

            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = ownerWindow;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);

            // Strictly filter for Executables
            ofn.lpstrFilter = L"Game Executables (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrFileTitle = NULL;
            ofn.nMaxFileTitle = 0;
            ofn.lpstrInitialDir = NULL;

            // Force the dialog to only accept existing files and preserve the working directory
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

            if (GetOpenFileNameW(&ofn) == TRUE) {
                // Convert the wide string path to standard string
                std::wstring ws(szFile);
                std::string fullPath(ws.begin(), ws.end());

                // Game detection matches on the file name only - "cs2.exe", not "C:\Steam\...\cs2.exe"
                // This slices off the directory structure safely
                size_t pos = fullPath.find_last_of("\\/");
                if (pos != std::string::npos) {
                    return fullPath.substr(pos + 1);
                }
                return fullPath;
            }

            // Return empty if the user hits "Cancel"
            return "";
        }

        // Returns the FULL path (unlike SelectExecutable, which strips it down to just
        // the exe name for game detection purposes) - export needs a real save location.
        static std::wstring SaveProfileFile(HWND ownerWindow) {
            OPENFILENAMEW ofn;
            wchar_t szFile[MAX_PATH] = L"OptiMaxProfile.optiprofile";

            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = ownerWindow;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
            ofn.lpstrFilter = L"OptiMax Profile (*.optiprofile)\0*.optiprofile\0All Files (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrDefExt = L"optiprofile";
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

            if (GetSaveFileNameW(&ofn) == TRUE) return std::wstring(szFile);
            return L"";
        }

        static std::wstring OpenProfileFile(HWND ownerWindow) {
            OPENFILENAMEW ofn;
            wchar_t szFile[MAX_PATH] = { 0 };

            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = ownerWindow;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
            ofn.lpstrFilter = L"OptiMax Profile (*.optiprofile)\0*.optiprofile\0All Files (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

            if (GetOpenFileNameW(&ofn) == TRUE) return std::wstring(szFile);
            return L"";
        }

        // Profile picture picker. Returns the FULL path, since the file has to be
        // read and uploaded. Restricted to the formats WIC can decode and Supabase
        // Storage is happy to serve back - no point letting someone pick a .bmp
        // that then fails on the way in.
        static std::wstring SelectImageFile(HWND ownerWindow) {
            OPENFILENAMEW ofn;
            wchar_t szFile[MAX_PATH] = { 0 };

            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = ownerWindow;
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
            ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0All Files (*.*)\0*.*\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

            if (GetOpenFileNameW(&ofn) == TRUE) return std::wstring(szFile);
            return L"";
        }
    };
}