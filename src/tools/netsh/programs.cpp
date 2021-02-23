// Copyright (C) Microsoft.
// SPDX-License-Identifier: MIT
#include <string>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <netsh.h>
#include "programs.h"
#include "tokens.h"

#include "api.h"
#include <iostream>
#pragma comment(lib, "EbpfApi.lib")

static HANDLE _program_handle = INVALID_HANDLE_VALUE;

typedef enum {
    PINNED_ANY = 0,
    PINNED_YES = 1,
    PINNED_NO = 2,
} PINNED_CONSTRAINT;

static TOKEN_VALUE _pinned_enum[] = {
    { L"any", PINNED_ANY },
    { L"yes", PINNED_YES },
    { L"no", PINNED_NO },
};

static TOKEN_VALUE _ebpf_program_type_enum[] = {
    { L"xdp", EBPF_PROGRAM_TYPE_XDP },
};

unsigned long handle_ebpf_add_program(
    LPCWSTR machine,
    LPWSTR* argv,
    DWORD current_index,
    DWORD argc,
    DWORD flags,
    LPCVOID data,
    BOOL* done)
{
    TAG_TYPE tags[] = {
        {TOKEN_FILENAME, NS_REQ_PRESENT, FALSE},
        {TOKEN_SECTION, NS_REQ_ZERO, FALSE},
        {TOKEN_TYPE, NS_REQ_ZERO, FALSE},
        {TOKEN_PINNED, NS_REQ_ZERO, FALSE},
    };
    ULONG tag_type[_countof(tags)] = { 0 };

    ULONG status = PreprocessCommand(nullptr,
        argv,
        current_index,
        argc,
        tags,
        _countof(tags),
        0,
        _countof(tags),
        tag_type);

    std::string filename;
    std::string section = ".text";
    EBPF_PROGRAM_TYPE type = EBPF_PROGRAM_TYPE_XDP;
    PINNED_CONSTRAINT pinned = PINNED_ANY;
    for (int i = 0; (status == NO_ERROR) && ((i + current_index) < argc); i++) {
        switch (tag_type[i]) {
        case 0: // FILENAME
        {
            std::wstring ws(argv[current_index + i]);
            filename = std::string(ws.begin(), ws.end());
            break;
        }
        case 1: // SECTION
        {
            std::wstring ws(argv[current_index + i]);
            section = std::string(ws.begin(), ws.end());
            break;
        }
        case 2: // TYPE
            status = MatchEnumTag(NULL,
                argv[current_index + i],
                _countof(_ebpf_program_type_enum),
                _ebpf_program_type_enum,
                (PULONG)&type);
            if (status != NO_ERROR) {
                status = ERROR_INVALID_PARAMETER;
            }
            break;
        case 3: // PINNED
            status = MatchEnumTag(NULL,
                argv[current_index + i],
                _countof(_pinned_enum),
                _pinned_enum,
                (PULONG)&pinned);
            if (status != NO_ERROR) {
                status = ERROR_INVALID_PARAMETER;
            }
            break;
        default:
            status = ERROR_INVALID_SYNTAX;
            break;
        }
    }
    if (status != NO_ERROR) {
        return status;
    }

    if (_program_handle != INVALID_HANDLE_VALUE)
    {
        ebpf_api_detach_program(_program_handle, EBPF_HOOK_POINT_XDP);
        ebpf_api_unload_program(_program_handle);
        _program_handle = INVALID_HANDLE_VALUE;
    }

    char* error_message = nullptr;

    status = ebpf_api_load_program(filename.c_str(), section.c_str(), &_program_handle, &error_message);

    if (status != ERROR_SUCCESS)
    {
        std::cerr << "ebpf_api_load_program failed with error " << status << " and message " << error_message << std::endl;
        return status;
    }

    status = ebpf_api_attach_program(_program_handle, EBPF_HOOK_POINT_XDP);
    if (status != ERROR_SUCCESS)
    {
        std::cerr << "ebpf_api_attach_program failed with error " << status << std::endl;
        return status;
    }
    return ERROR_SUCCESS;
}

DWORD handle_ebpf_delete_program(
    LPCWSTR machine,
    LPWSTR* argv,
    DWORD current_index,
    DWORD argc,
    DWORD flags,
    LPCVOID data,
    BOOL* done)
{
    TAG_TYPE tags[] = {
        {TOKEN_FILENAME, NS_REQ_PRESENT, FALSE},
        {TOKEN_SECTION, NS_REQ_ZERO, FALSE},
    };
    ULONG tag_type[_countof(tags)] = { 0 };

    ULONG status = PreprocessCommand(nullptr,
        argv,
        current_index,
        argc,
        tags,
        _countof(tags),
        0,
        _countof(tags),
        tag_type);

    std::string filename;
    std::string section = ".text";
    for (int i = 0; (status == NO_ERROR) && ((i + current_index) < argc); i++) {
        switch (tag_type[i]) {
        case 0: // FILENAME
        {
            std::wstring ws(argv[current_index + i]);
            filename = std::string(ws.begin(), ws.end());
            break;
        }
        case 1: // SECTION
        {
            std::wstring ws(argv[current_index + i]);
            section = std::string(ws.begin(), ws.end());
            break;
        }
        default:
            status = ERROR_INVALID_SYNTAX;
            break;
        }
    }

    if (_program_handle != INVALID_HANDLE_VALUE)
    {
        ebpf_api_detach_program(_program_handle, EBPF_HOOK_POINT_XDP);
        ebpf_api_unload_program(_program_handle);
        _program_handle = INVALID_HANDLE_VALUE;
    }

    // TODO: delete program
    return ERROR_SUCCESS;
}

DWORD handle_ebpf_set_program(
    LPCWSTR machine,
    LPWSTR* argv,
    DWORD current_index,
    DWORD argc,
    DWORD flags,
    LPCVOID data,
    BOOL* done)
{
    TAG_TYPE tags[] = {
        {TOKEN_FILENAME, NS_REQ_PRESENT, FALSE},
        {TOKEN_SECTION, NS_REQ_PRESENT, FALSE},
        {TOKEN_PINNED, NS_REQ_ZERO, FALSE},
    };
    ULONG tag_type[_countof(tags)] = { 0 };

    ULONG status = PreprocessCommand(nullptr,
        argv,
        current_index,
        argc,
        tags,
        _countof(tags),
        0,
        3, // Two required tags plus at least one optional tag.
        tag_type);

    std::string filename;
    std::string section = ".text";
    PINNED_CONSTRAINT pinned = PINNED_ANY;
    for (int i = 0; (status == NO_ERROR) && ((i + current_index) < argc); i++) {
        switch (tag_type[i]) {
        case 0: // FILENAME
        {
            std::wstring ws(argv[current_index + i]);
            filename = std::string(ws.begin(), ws.end());
            break;
        }
        case 1: // SECTION
        {
            std::wstring ws(argv[current_index + i]);
            section = std::string(ws.begin(), ws.end());
            break;
        }
        case 2: // PINNED
            status = MatchEnumTag(NULL,
                argv[current_index + i],
                _countof(_pinned_enum),
                _pinned_enum,
                (PULONG)&pinned);
            if ((status != NO_ERROR) || (pinned == PINNED_ANY)) {
                status = ERROR_INVALID_PARAMETER;
            }
            break;
        default:
            status = ERROR_INVALID_SYNTAX;
            break;
        }
    }
    if (status != NO_ERROR) {
        return status;
    }

    // TODO: update program
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD handle_ebpf_show_programs(
    LPCWSTR machine,
    LPWSTR* argv,
    DWORD current_index,
    DWORD argc,
    DWORD flags,
    LPCVOID data,
    BOOL* done)
{
    TAG_TYPE tags[] = {
        {TOKEN_TYPE, NS_REQ_ZERO, FALSE},
        {TOKEN_PINNED, NS_REQ_ZERO, FALSE},
        {TOKEN_LEVEL, NS_REQ_ZERO, FALSE},
        {TOKEN_FILENAME, NS_REQ_ZERO, FALSE},
        {TOKEN_SECTION, NS_REQ_ZERO, FALSE},
    };
    ULONG tag_type[_countof(tags)] = { 0 };

    ULONG status = PreprocessCommand(nullptr,
        argv,
        current_index,
        argc,
        tags,
        _countof(tags),
        0,
        _countof(tags),
        tag_type);

    EBPF_PROGRAM_TYPE type = EBPF_PROGRAM_TYPE_XDP;
    PINNED_CONSTRAINT pinned = PINNED_ANY;
    VERBOSITY_LEVEL level = VL_NORMAL;
    std::string filename;
    std::string section = ".text";
    for (int i = 0; (status == NO_ERROR) && ((i + current_index) < argc); i++) {
        switch (tag_type[i]) {
        case 0: // TYPE
            status = MatchEnumTag(NULL,
                argv[current_index + i],
                _countof(_ebpf_program_type_enum),
                _ebpf_program_type_enum,
                (PULONG)&type);
            if (status != NO_ERROR) {
                status = ERROR_INVALID_PARAMETER;
            }
            break;
        case 1: // PINNED
            status = MatchEnumTag(NULL,
                argv[current_index + i],
                _countof(_pinned_enum),
                _pinned_enum,
                (PULONG)&pinned);
            if (status != NO_ERROR) {
                status = ERROR_INVALID_PARAMETER;
            }
            break;
        case 2: // LEVEL
            status = MatchEnumTag(NULL,
                argv[current_index + i],
                _countof(g_LevelEnum),
                g_LevelEnum,
                (PULONG)&level);
            if (status != NO_ERROR) {
                status = ERROR_INVALID_PARAMETER;
            }
            break;
        case 3: // FILENAME
        {
            std::wstring ws(argv[current_index + i]);
            filename = std::string(ws.begin(), ws.end());
            break;
        }
        case 4: // SECTION
        {
            std::wstring ws(argv[current_index + i]);
            section = std::string(ws.begin(), ws.end());
            break;
        }
        default:
            status = ERROR_INVALID_SYNTAX;
            break;
        }
    }
    if (status != NO_ERROR) {
        return status;
    }

    // If the user specified a filename and no level, default to verbose.
    if (tags[3].bPresent && !tags[2].bPresent) {
        level = VL_VERBOSE;
    }

    // TODO: enumerate programs using specified constraints
    return ERROR_CALL_NOT_IMPLEMENTED;
}