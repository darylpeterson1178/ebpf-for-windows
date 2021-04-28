/*
 *  Copyright (c) Microsoft Corporation
 *  SPDX-License-Identifier: MIT
 */

#define CATCH_CONFIG_MAIN

#include <chrono>
#include <mutex>
#include <thread>
#include <WinSock2.h>

#include "catch2\catch.hpp"
#include "ebpf_api.h"
#include "ebpf_core.h"
#include "ebpf_epoch.h"
#include "ebpf_pinning_table.h"
#include "ebpf_protocol.h"
#include "mock.h"
#include "tlv.h"
namespace ebpf {
#pragma warning(push)
#pragma warning(disable : 4201) // nonstandard extension used : nameless struct/union
#include "../sample/ebpf.h"
#pragma warning(pop)
}; // namespace ebpf

#include "unwind_helper.h"

#define EBPF_UTF8_STRING_FROM_CONST_STRING(x) \
    {                                         \
        ((uint8_t*)x), sizeof((x)) - 1        \
    }

ebpf_handle_t
GlueCreateFileW(
    PCWSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    PSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    ebpf_handle_t hTemplateFile)
{
    UNREFERENCED_PARAMETER(lpFileName);
    UNREFERENCED_PARAMETER(dwDesiredAccess);
    UNREFERENCED_PARAMETER(dwShareMode);
    UNREFERENCED_PARAMETER(lpSecurityAttributes);
    UNREFERENCED_PARAMETER(dwCreationDisposition);
    UNREFERENCED_PARAMETER(dwFlagsAndAttributes);
    UNREFERENCED_PARAMETER(hTemplateFile);

    return (ebpf_handle_t)0x12345678;
}

BOOL
GlueCloseHandle(ebpf_handle_t hObject)
{
    UNREFERENCED_PARAMETER(hObject);
    return TRUE;
}

BOOL
GlueDeviceIoControl(
    ebpf_handle_t hDevice,
    DWORD dwIoControlCode,
    PVOID lpInBuffer,
    DWORD nInBufferSize,
    LPVOID lpOutBuffer,
    DWORD nOutBufferSize,
    PDWORD lpBytesReturned,
    OVERLAPPED* lpOverlapped)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(nInBufferSize);
    UNREFERENCED_PARAMETER(dwIoControlCode);
    UNREFERENCED_PARAMETER(lpOverlapped);

    ebpf_error_code_t retval;
    const ebpf_operation_header_t* user_request = reinterpret_cast<decltype(user_request)>(lpInBuffer);
    ebpf_operation_header_t* user_reply = nullptr;
    *lpBytesReturned = 0;
    auto request_id = user_request->id;
    size_t minimum_request_size = 0;
    size_t minimum_reply_size = 0;

    retval = ebpf_core_get_protocol_handler_properties(request_id, &minimum_request_size, &minimum_reply_size);
    if (retval != EBPF_ERROR_SUCCESS)
        goto Fail;

    if (user_request->length < minimum_request_size) {
        retval = EBPF_ERROR_INVALID_PARAMETER;
        goto Fail;
    }

    if (minimum_reply_size > 0) {
        user_reply = reinterpret_cast<decltype(user_reply)>(lpOutBuffer);
        if (!user_reply) {
            retval = EBPF_ERROR_INVALID_PARAMETER;
            goto Fail;
        }
        if (nOutBufferSize < minimum_reply_size) {
            retval = EBPF_ERROR_INVALID_PARAMETER;
            goto Fail;
        }
        user_reply->length = static_cast<uint16_t>(nOutBufferSize);
        user_reply->id = user_request->id;
        *lpBytesReturned = user_reply->length;
    }

    retval =
        ebpf_core_invoke_protocol_handler(request_id, user_request, user_reply, static_cast<uint16_t>(nOutBufferSize));

    if (retval != EBPF_ERROR_SUCCESS)
        goto Fail;

    return TRUE;

Fail:
    if (retval != EBPF_ERROR_SUCCESS) {
        switch (retval) {
        case EBPF_ERROR_OUT_OF_RESOURCES:
            SetLastError(ERROR_OUTOFMEMORY);
            break;
        case EBPF_ERROR_NOT_FOUND:
            SetLastError(ERROR_NOT_FOUND);
            break;
        case EBPF_ERROR_INVALID_PARAMETER:
            SetLastError(ERROR_INVALID_PARAMETER);
            break;
        case EBPF_ERROR_NO_MORE_KEYS:
            SetLastError(ERROR_NO_MORE_ITEMS);
            break;
        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            break;
        }
    }

    return FALSE;
}

std::vector<uint8_t>
prepare_udp_packet(uint16_t udp_length)
{
    std::vector<uint8_t> packet(sizeof(ebpf::IPV4_HEADER) + sizeof(ebpf::UDP_HEADER));
    auto ipv4 = reinterpret_cast<ebpf::IPV4_HEADER*>(packet.data());
    auto udp = reinterpret_cast<ebpf::UDP_HEADER*>(ipv4 + 1);

    ipv4->Protocol = 17;

    udp->length = udp_length;

    return packet;
}

typedef class _single_instance_hook
{
  public:
    _single_instance_hook()
    {
        ebpf_guid_create(&attach_type);

        REQUIRE(
            ebpf_provider_load(
                &provider,
                &attach_type,
                NULL,
                &provider_data,
                NULL,
                this,
                client_attach_callback,
                client_detach_callback) == EBPF_ERROR_SUCCESS);
    }
    ~_single_instance_hook() { epbf_provider_unload(provider); }

    uint32_t
    attach(ebpf_handle_t program_handle)
    {
        return ebpf_api_link_program(program_handle, attach_type, &link_handle);
    }

    void
    detach()
    {
        ebpf_api_close_handle(link_handle);
    }

    ebpf_error_code_t
    fire(void* context, uint32_t* result)
    {
        ebpf_error_code_t (*invoke_program)(void* link, void* context, uint32_t* result) =
            reinterpret_cast<decltype(invoke_program)>(client_dispatch_table->function[0]);

        return invoke_program(client_binding_context, context, result);
    }

  private:
    static ebpf_error_code_t
    client_attach_callback(
        void* context,
        const GUID* client_id,
        void* client_binding_context,
        const ebpf_extension_data_t* client_data,
        const ebpf_extension_dispatch_table_t* client_dispatch_table)
    {
        auto hook = reinterpret_cast<_single_instance_hook*>(context);
        hook->client_id = *client_id;
        hook->client_binding_context = client_binding_context;
        hook->client_data = client_data;
        hook->client_dispatch_table = client_dispatch_table;
        return EBPF_ERROR_SUCCESS;
    };

    static ebpf_error_code_t
    client_detach_callback(void* context, const GUID* client_id)
    {
        auto hook = reinterpret_cast<_single_instance_hook*>(context);
        hook->client_binding_context = NULL;
        hook->client_data = NULL;
        hook->client_dispatch_table = NULL;
        UNREFERENCED_PARAMETER(client_id);
        return EBPF_ERROR_SUCCESS;
    };
    ebpf_attach_type_t attach_type;

    ebpf_extension_data_t provider_data = {0, 0};
    ebpf_extension_provider_t* provider;
    GUID client_id;
    void* client_binding_context;
    const ebpf_extension_data_t* client_data;
    const ebpf_extension_dispatch_table_t* client_dispatch_table;
    ebpf_handle_t link_handle;
} single_instance_hook_t;

#define SAMPLE_PATH ""

TEST_CASE("pinning_test", "[pinning_test]")
{

    bool platform_initiated;
    _unwind_helper on_exit([&] {
        if (platform_initiated)
            ebpf_platform_terminate();
    });

    typedef struct _some_object
    {
        ebpf_object_t object;
        std::string name;
    } some_object_t;

    REQUIRE(ebpf_platform_initiate() == EBPF_ERROR_SUCCESS);

    some_object_t an_object;
    some_object_t another_object;
    some_object_t* some_object;
    ebpf_utf8_string_t foo = EBPF_UTF8_STRING_FROM_CONST_STRING("foo");
    ebpf_utf8_string_t bar = EBPF_UTF8_STRING_FROM_CONST_STRING("bar");

    ebpf_object_initiate(&an_object.object, EBPF_OBJECT_MAP, [](ebpf_object_t*) {});
    ebpf_object_initiate(&another_object.object, EBPF_OBJECT_MAP, [](ebpf_object_t*) {});

    ebpf_pinning_table_t* pinning_table;
    REQUIRE(ebpf_pinning_table_allocate(&pinning_table) == EBPF_ERROR_SUCCESS);

    REQUIRE(ebpf_pinning_table_insert(pinning_table, &foo, &an_object.object) == EBPF_ERROR_SUCCESS);
    REQUIRE(an_object.object.reference_count == 2);
    REQUIRE(ebpf_pinning_table_insert(pinning_table, &bar, &another_object.object) == EBPF_ERROR_SUCCESS);
    REQUIRE(another_object.object.reference_count == 2);
    REQUIRE(ebpf_pinning_table_find(pinning_table, &foo, (ebpf_object_t**)&some_object) == EBPF_ERROR_SUCCESS);
    REQUIRE(an_object.object.reference_count == 3);
    REQUIRE(some_object == &an_object);
    ebpf_object_release_reference(&some_object->object);
    REQUIRE(ebpf_pinning_table_delete(pinning_table, &foo) == EBPF_ERROR_SUCCESS);
    REQUIRE(another_object.object.reference_count == 2);

    ebpf_pinning_table_free(pinning_table);
    REQUIRE(an_object.object.reference_count == 1);
    REQUIRE(another_object.object.reference_count == 1);
}

TEST_CASE("droppacket-jit", "[droppacket_jit]")
{
    device_io_control_handler = GlueDeviceIoControl;
    create_file_handler = GlueCreateFileW;
    close_handle_handler = GlueCloseHandle;

    ebpf_handle_t program_handle;
    ebpf_handle_t map_handle;
    uint32_t count_of_map_handle = 1;
    uint32_t result = 0;
    const char* error_message = NULL;
    bool ec_initialized = false;
    bool api_initialized = false;
    _unwind_helper on_exit([&] {
        ebpf_api_free_string(error_message);
        if (api_initialized)
            ebpf_api_terminate();
        if (ec_initialized)
            ebpf_core_terminate();
    });

    REQUIRE(ebpf_core_initiate() == EBPF_ERROR_SUCCESS);
    ec_initialized = true;

    single_instance_hook_t hook;

    REQUIRE(ebpf_api_initiate() == ERROR_SUCCESS);
    api_initialized = true;

    REQUIRE(
        ebpf_api_load_program(
            SAMPLE_PATH "droppacket.o",
            "xdp",
            EBPF_EXECUTION_JIT,
            &program_handle,
            &count_of_map_handle,
            &map_handle,
            &error_message) == ERROR_SUCCESS);

    REQUIRE(hook.attach(program_handle) == ERROR_SUCCESS);

    auto packet = prepare_udp_packet(0);

    uint32_t key = 0;
    uint64_t value = 1000;
    REQUIRE(
        ebpf_api_map_update_element(map_handle, sizeof(key), (uint8_t*)&key, sizeof(value), (uint8_t*)&value) ==
        ERROR_SUCCESS);

    // Test that we drop the packet and increment the map
    ebpf::xdp_md_t ctx{packet.data(), packet.data() + packet.size()};

    REQUIRE(hook.fire(&ctx, &result) == EBPF_ERROR_SUCCESS);
    REQUIRE(result == 2);

    REQUIRE(
        ebpf_api_map_find_element(map_handle, sizeof(key), (uint8_t*)&key, sizeof(value), (uint8_t*)&value) ==
        ERROR_SUCCESS);
    REQUIRE(value == 1001);

    REQUIRE(ebpf_api_map_delete_element(map_handle, sizeof(key), (uint8_t*)&key) == ERROR_SUCCESS);

    REQUIRE(
        ebpf_api_map_find_element(map_handle, sizeof(key), (uint8_t*)&key, sizeof(value), (uint8_t*)&value) ==
        ERROR_SUCCESS);
    REQUIRE(value == 0);

    packet = prepare_udp_packet(10);
    ebpf::xdp_md_t ctx2{packet.data(), packet.data() + packet.size()};

    REQUIRE(hook.fire(&ctx2, &result) == EBPF_ERROR_SUCCESS);
    REQUIRE(result == 1);

    REQUIRE(
        ebpf_api_map_find_element(map_handle, sizeof(key), (uint8_t*)&key, sizeof(value), (uint8_t*)&value) ==
        ERROR_SUCCESS);
    REQUIRE(value == 0);

    hook.detach();
}

TEST_CASE("droppacket-interpret", "[droppacket_interpret]")
{
    device_io_control_handler = GlueDeviceIoControl;
    create_file_handler = GlueCreateFileW;
    close_handle_handler = GlueCloseHandle;

    ebpf_handle_t program_handle;
    const char* error_message = NULL;
    bool ec_initialized = false;
    bool api_initialized = false;
    ebpf_handle_t map_handle;
    uint32_t count_of_map_handle = 1;
    _unwind_helper on_exit([&] {
        ebpf_api_free_string(error_message);
        if (api_initialized)
            ebpf_api_terminate();
        if (ec_initialized)
            ebpf_core_terminate();
    });
    uint32_t result = 0;

    REQUIRE(ebpf_core_initiate() == EBPF_ERROR_SUCCESS);
    ec_initialized = true;

    REQUIRE(ebpf_api_initiate() == ERROR_SUCCESS);
    api_initialized = true;

    single_instance_hook_t hook;

    REQUIRE(
        ebpf_api_load_program(
            SAMPLE_PATH "droppacket.o",
            "xdp",
            EBPF_EXECUTION_INTERPRET,
            &program_handle,
            &count_of_map_handle,
            &map_handle,
            &error_message) == ERROR_SUCCESS);

    REQUIRE(hook.attach(program_handle) == ERROR_SUCCESS);

    auto packet = prepare_udp_packet(0);

    uint32_t key = 0;
    uint64_t value = 1000;
    REQUIRE(
        ebpf_api_map_update_element((ebpf_handle_t)1, sizeof(key), (uint8_t*)&key, sizeof(value), (uint8_t*)&value) ==
        ERROR_SUCCESS);

    // Test that we drop the packet and increment the map
    ebpf::xdp_md_t ctx{packet.data(), packet.data() + packet.size()};
    REQUIRE(hook.fire(&ctx, &result) == EBPF_ERROR_SUCCESS);
    REQUIRE(result == 2);

    REQUIRE(
        ebpf_api_map_find_element((ebpf_handle_t)1, sizeof(key), (uint8_t*)&key, sizeof(value), (uint8_t*)&value) ==
        ERROR_SUCCESS);
    REQUIRE(value == 1001);

    REQUIRE(ebpf_api_map_delete_element((ebpf_handle_t)1, sizeof(key), (uint8_t*)&key) == ERROR_SUCCESS);

    REQUIRE(
        ebpf_api_map_find_element((ebpf_handle_t)1, sizeof(key), (uint8_t*)&key, sizeof(value), (uint8_t*)&value) ==
        ERROR_SUCCESS);
    REQUIRE(value == 0);

    packet = prepare_udp_packet(10);
    ebpf::xdp_md_t ctx2{packet.data(), packet.data() + packet.size()};

    REQUIRE(hook.fire(&ctx2, &result) == EBPF_ERROR_SUCCESS);
    REQUIRE(result == 1);

    REQUIRE(
        ebpf_api_map_find_element((ebpf_handle_t)1, sizeof(key), (uint8_t*)&key, sizeof(value), (uint8_t*)&value) ==
        ERROR_SUCCESS);
    REQUIRE(value == 0);
}

TEST_CASE("enum section", "[enum sections]")
{
    const char* error_message = nullptr;
    const tlv_type_length_value_t* section_data = nullptr;
    bool ec_initialized = false;
    bool api_initialized = false;
    _unwind_helper on_exit([&] {
        ebpf_api_free_string(error_message);
        ebpf_api_elf_free(section_data);
        if (api_initialized)
            ebpf_api_terminate();
        if (ec_initialized)
            ebpf_core_terminate();
    });

    REQUIRE(ebpf_core_initiate() == EBPF_ERROR_SUCCESS);
    ec_initialized = true;
    REQUIRE(ebpf_api_initiate() == ERROR_SUCCESS);
    api_initialized = true;

    REQUIRE(
        ebpf_api_elf_enumerate_sections(SAMPLE_PATH "droppacket.o", nullptr, true, &section_data, &error_message) == 0);
    for (auto current_section = tlv_child(section_data); current_section != tlv_next(section_data);
         current_section = tlv_next(current_section)) {
        auto section_name = tlv_child(current_section);
        auto type = tlv_next(section_name);
        auto map_count = tlv_next(type);
        auto program_bytes = tlv_next(map_count);
        auto stats_secton = tlv_next(program_bytes);

        REQUIRE(static_cast<tlv_type_t>(section_name->type) == tlv_type_t::STRING);
        REQUIRE(static_cast<tlv_type_t>(type->type) == tlv_type_t::UINT);
        REQUIRE(static_cast<tlv_type_t>(map_count->type) == tlv_type_t::UINT);
        REQUIRE(static_cast<tlv_type_t>(program_bytes->type) == tlv_type_t::BLOB);
        REQUIRE(static_cast<tlv_type_t>(stats_secton->type) == tlv_type_t::SEQUENCE);

        for (auto current_stat = tlv_child(stats_secton); current_stat != tlv_next(stats_secton);
             current_stat = tlv_next(current_stat)) {
            auto name = tlv_child(current_stat);
            auto value = tlv_next(name);
            REQUIRE(static_cast<tlv_type_t>(name->type) == tlv_type_t::STRING);
            REQUIRE(static_cast<tlv_type_t>(value->type) == tlv_type_t::UINT);
        }
    }
}

TEST_CASE("verify section", "[verify section]")
{

    const char* error_message = nullptr;
    const char* report = nullptr;
    bool ec_initialized = false;
    bool api_initialized = false;
    _unwind_helper on_exit([&] {
        ebpf_api_free_string(error_message);
        ebpf_api_free_string(report);
        if (api_initialized)
            ebpf_api_terminate();
        if (ec_initialized)
            ebpf_core_terminate();
    });

    REQUIRE(ebpf_core_initiate() == EBPF_ERROR_SUCCESS);
    api_initialized = true;
    REQUIRE(ebpf_api_initiate() == ERROR_SUCCESS);
    ec_initialized = true;

    REQUIRE(ebpf_api_elf_verify_section(SAMPLE_PATH "droppacket.o", "xdp", false, &report, &error_message) == 0);
    REQUIRE(report != nullptr);
    REQUIRE(error_message == nullptr);
}

typedef struct _process_entry
{
    uint32_t count;
    uint8_t name[64];
    uint64_t appid_length;
} process_entry_t;

uint32_t
get_bind_count_for_pid(ebpf_handle_t handle, uint64_t pid)
{
    process_entry_t entry{};
    ebpf_api_map_find_element(handle, sizeof(pid), (uint8_t*)&pid, sizeof(entry), (uint8_t*)&entry);

    return entry.count;
}

ebpf::bind_action_t
emulate_bind(single_instance_hook_t& hook, uint64_t pid, const char* appid)
{
    uint32_t result;
    std::string app_id = appid;
    ebpf::bind_md_t ctx{0};
    ctx.app_id_start = const_cast<char*>(app_id.c_str());
    ctx.app_id_end = const_cast<char*>(app_id.c_str()) + app_id.size();
    ctx.process_id = pid;
    ctx.operation = ebpf::BIND_OPERATION_BIND;
    REQUIRE(hook.fire(&ctx, &result) == EBPF_ERROR_SUCCESS);
    return static_cast<ebpf::bind_action_t>(result);
}

void
emulate_unbind(single_instance_hook_t& hook, uint64_t pid, const char* appid)
{
    uint32_t result;
    std::string app_id = appid;
    ebpf::bind_md_t ctx{0};
    ctx.process_id = pid;
    ctx.operation = ebpf::BIND_OPERATION_UNBIND;
    REQUIRE(hook.fire(&ctx, &result) == EBPF_ERROR_SUCCESS);
}

void
set_bind_limit(ebpf_handle_t handle, uint32_t limit)
{
    uint32_t limit_key = 0;
    REQUIRE(
        ebpf_api_map_update_element(handle, sizeof(limit_key), (uint8_t*)&limit_key, sizeof(limit), (uint8_t*)&limit) ==
        ERROR_SUCCESS);
}

TEST_CASE("bindmonitor-interpret", "[bindmonitor_interpret]")
{
    device_io_control_handler = GlueDeviceIoControl;
    create_file_handler = GlueCreateFileW;
    close_handle_handler = GlueCloseHandle;

    ebpf_handle_t program_handle;
    const char* error_message = NULL;
    bool ec_initialized = false;
    bool api_initialized = false;
    ebpf_handle_t map_handles[4];
    uint32_t count_of_map_handles = 2;
    uint64_t fake_pid = 12345;

    _unwind_helper on_exit([&] {
        ebpf_api_free_string(error_message);
        if (api_initialized)
            ebpf_api_terminate();
        if (ec_initialized)
            ebpf_core_terminate();
    });

    REQUIRE(ebpf_core_initiate() == EBPF_ERROR_SUCCESS);
    ec_initialized = true;

    REQUIRE(ebpf_api_initiate() == ERROR_SUCCESS);
    api_initialized = true;

    REQUIRE(
        ebpf_api_load_program(
            SAMPLE_PATH "bindmonitor.o",
            "bind",
            EBPF_EXECUTION_INTERPRET,
            &program_handle,
            &count_of_map_handles,
            map_handles,
            &error_message) == ERROR_SUCCESS);
    REQUIRE(error_message == NULL);

    single_instance_hook_t hook;

    std::string process_maps_name = "bindmonitor::process_maps";
    std::string limit_maps_name = "bindmonitor::limits_map";

    REQUIRE(
        ebpf_api_pin_map(
            map_handles[0],
            reinterpret_cast<const uint8_t*>(process_maps_name.c_str()),
            static_cast<uint32_t>(process_maps_name.size())) == ERROR_SUCCESS);
    REQUIRE(
        ebpf_api_pin_map(
            map_handles[1],
            reinterpret_cast<const uint8_t*>(limit_maps_name.c_str()),
            static_cast<uint32_t>(limit_maps_name.size())) == ERROR_SUCCESS);

    ebpf_handle_t test_handle;
    REQUIRE(
        ebpf_api_get_pinned_map(
            reinterpret_cast<const uint8_t*>(process_maps_name.c_str()),
            static_cast<uint32_t>(process_maps_name.size()),
            &map_handles[2]) == ERROR_SUCCESS);
    REQUIRE(
        ebpf_api_get_pinned_map(
            reinterpret_cast<const uint8_t*>(limit_maps_name.c_str()),
            static_cast<uint32_t>(limit_maps_name.size()),
            &map_handles[3]) == ERROR_SUCCESS);

    REQUIRE(
        ebpf_api_unpin_map(
            reinterpret_cast<const uint8_t*>(process_maps_name.c_str()),
            static_cast<uint32_t>(process_maps_name.size())) == ERROR_SUCCESS);
    REQUIRE(
        ebpf_api_unpin_map(
            reinterpret_cast<const uint8_t*>(limit_maps_name.c_str()), static_cast<uint32_t>(limit_maps_name.size())) ==
        ERROR_SUCCESS);
    REQUIRE(
        ebpf_api_get_pinned_map(
            reinterpret_cast<const uint8_t*>(process_maps_name.c_str()),
            static_cast<uint32_t>(process_maps_name.size()),
            &test_handle) == ERROR_NOT_FOUND);
    REQUIRE(
        ebpf_api_get_pinned_map(
            reinterpret_cast<const uint8_t*>(limit_maps_name.c_str()),
            static_cast<uint32_t>(limit_maps_name.size()),
            &test_handle) == ERROR_NOT_FOUND);

    ebpf_handle_t handle_iterator = INVALID_HANDLE_VALUE;
    REQUIRE(ebpf_api_get_next_map(handle_iterator, &handle_iterator) == ERROR_SUCCESS);
    REQUIRE(handle_iterator == map_handles[0]);
    REQUIRE(ebpf_api_get_next_map(handle_iterator, &handle_iterator) == ERROR_SUCCESS);
    REQUIRE(handle_iterator == map_handles[1]);
    REQUIRE(ebpf_api_get_next_map(handle_iterator, &handle_iterator) == ERROR_SUCCESS);
    REQUIRE(handle_iterator == map_handles[2]);
    REQUIRE(ebpf_api_get_next_map(handle_iterator, &handle_iterator) == ERROR_SUCCESS);
    REQUIRE(handle_iterator == map_handles[3]);
    REQUIRE(ebpf_api_get_next_map(handle_iterator, &handle_iterator) == ERROR_SUCCESS);
    REQUIRE(handle_iterator == INVALID_HANDLE_VALUE);

    hook.attach(program_handle);

    // Apply policy of maximum 2 binds per process
    set_bind_limit(map_handles[1], 2);

    // Bind first port - success
    REQUIRE(emulate_bind(hook, fake_pid, "fake_app_1") == ebpf::BIND_PERMIT);
    REQUIRE(get_bind_count_for_pid(map_handles[0], fake_pid) == 1);

    // Bind second port - success
    REQUIRE(emulate_bind(hook, fake_pid, "fake_app_1") == ebpf::BIND_PERMIT);
    REQUIRE(get_bind_count_for_pid(map_handles[0], fake_pid) == 2);

    // Bind third port - blocked
    REQUIRE(emulate_bind(hook, fake_pid, "fake_app_1") == ebpf::BIND_DENY);
    REQUIRE(get_bind_count_for_pid(map_handles[0], fake_pid) == 2);

    // Unbind second port
    emulate_unbind(hook, fake_pid, "fake_app_1");
    REQUIRE(get_bind_count_for_pid(map_handles[0], fake_pid) == 1);

    // Unbind first port
    emulate_unbind(hook, fake_pid, "fake_app_1");
    REQUIRE(get_bind_count_for_pid(map_handles[0], fake_pid) == 0);

    // Bind from two apps to test enumeration
    REQUIRE(emulate_bind(hook, fake_pid, "fake_app_1") == ebpf::BIND_PERMIT);
    REQUIRE(get_bind_count_for_pid(map_handles[0], fake_pid) == 1);

    fake_pid = 54321;
    REQUIRE(emulate_bind(hook, fake_pid, "fake_app_2") == ebpf::BIND_PERMIT);
    REQUIRE(get_bind_count_for_pid(map_handles[0], fake_pid) == 1);

    uint64_t pid;
    REQUIRE(
        ebpf_api_get_next_map_key(map_handles[0], sizeof(uint64_t), NULL, reinterpret_cast<uint8_t*>(&pid)) ==
        ERROR_SUCCESS);
    REQUIRE(pid != 0);
    REQUIRE(
        ebpf_api_get_next_map_key(
            map_handles[0], sizeof(uint64_t), reinterpret_cast<uint8_t*>(&pid), reinterpret_cast<uint8_t*>(&pid)) ==
        ERROR_SUCCESS);
    REQUIRE(pid != 0);
    REQUIRE(
        ebpf_api_get_next_map_key(
            map_handles[0], sizeof(uint64_t), reinterpret_cast<uint8_t*>(&pid), reinterpret_cast<uint8_t*>(&pid)) ==
        ERROR_NO_MORE_ITEMS);

    hook.detach();
}

TEST_CASE("enumerate_and_query_programs", "[enumerate_and_query_programs]")
{
    device_io_control_handler = GlueDeviceIoControl;
    create_file_handler = GlueCreateFileW;
    close_handle_handler = GlueCloseHandle;

    ebpf_handle_t program_handle;
    ebpf_handle_t map_handles[3];
    uint32_t count_of_map_handle = 1;
    const char* error_message = NULL;
    bool ec_initialized = false;
    bool api_initialized = false;
    const char* file_name = nullptr;
    const char* section_name = nullptr;

    _unwind_helper on_exit([&] {
        ebpf_api_free_string(error_message);
        if (api_initialized)
            ebpf_api_terminate();
        if (ec_initialized)
            ebpf_core_terminate();
        ebpf_api_free_string(file_name);
        ebpf_api_free_string(section_name);
    });

    REQUIRE(ebpf_core_initiate() == EBPF_ERROR_SUCCESS);
    ec_initialized = true;

    REQUIRE(ebpf_api_initiate() == ERROR_SUCCESS);
    api_initialized = true;

    REQUIRE(
        ebpf_api_load_program(
            SAMPLE_PATH "droppacket.o",
            "xdp",
            EBPF_EXECUTION_JIT,
            &program_handle,
            &count_of_map_handle,
            map_handles,
            &error_message) == ERROR_SUCCESS);

    REQUIRE(
        ebpf_api_load_program(
            SAMPLE_PATH "droppacket.o",
            "xdp",
            EBPF_EXECUTION_INTERPRET,
            &program_handle,
            &count_of_map_handle,
            map_handles,
            &error_message) == ERROR_SUCCESS);

    ebpf_execution_type_t type;
    program_handle = INVALID_HANDLE_VALUE;
    REQUIRE(ebpf_api_get_next_program(program_handle, &program_handle) == ERROR_SUCCESS);
    REQUIRE(ebpf_api_program_query_information(program_handle, &type, &file_name, &section_name) == ERROR_SUCCESS);
    REQUIRE(type == EBPF_EXECUTION_JIT);
    REQUIRE(strcmp(file_name, SAMPLE_PATH "droppacket.o") == 0);
    REQUIRE(strcmp(section_name, "xdp") == 0);
    REQUIRE(program_handle != INVALID_HANDLE_VALUE);
    ebpf_api_free_string(file_name);
    ebpf_api_free_string(section_name);
    file_name = nullptr;
    section_name = nullptr;
    REQUIRE(ebpf_api_get_next_program(program_handle, &program_handle) == ERROR_SUCCESS);
    REQUIRE(program_handle != INVALID_HANDLE_VALUE);
    REQUIRE(ebpf_api_program_query_information(program_handle, &type, &file_name, &section_name) == ERROR_SUCCESS);
    REQUIRE(type == EBPF_EXECUTION_INTERPRET);
    REQUIRE(strcmp(file_name, SAMPLE_PATH "droppacket.o") == 0);
    REQUIRE(strcmp(section_name, "xdp") == 0);
    ebpf_api_free_string(file_name);
    ebpf_api_free_string(section_name);
    file_name = nullptr;
    section_name = nullptr;
    REQUIRE(ebpf_api_get_next_program(program_handle, &program_handle) == ERROR_SUCCESS);
    REQUIRE(program_handle == INVALID_HANDLE_VALUE);
}

TEST_CASE("epoch_test_single_epoch", "[epoch_test_single_epoch]")
{
    bool ep_initialized = false;
    _unwind_helper on_exit([&] {
        if (ep_initialized)
            ebpf_epoch_terminate();
    });

    REQUIRE(ebpf_epoch_initiate() == EBPF_ERROR_SUCCESS);
    ep_initialized = true;

    REQUIRE(ebpf_epoch_enter() == EBPF_ERROR_SUCCESS);
    void* memory = ebpf_epoch_allocate(10, EBPF_MEMORY_NO_EXECUTE);
    ebpf_epoch_free(memory);
    ebpf_epoch_exit();
    ebpf_epoch_flush();
}

TEST_CASE("epoch_test_two_threads", "[epoch_test_two_threads]")
{
    bool ep_initialized = false;
    _unwind_helper on_exit([&] {
        if (ep_initialized)
            ebpf_epoch_terminate();
    });

    REQUIRE(ebpf_epoch_initiate() == EBPF_ERROR_SUCCESS);
    ep_initialized = true;

    auto epoch = []() {
        ebpf_epoch_enter();
        void* memory = ebpf_epoch_allocate(10, EBPF_MEMORY_NO_EXECUTE);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        ebpf_epoch_free(memory);
        ebpf_epoch_exit();
        ebpf_epoch_flush();
    };

    std::thread thread_1(epoch);
    std::thread thread_2(epoch);
    thread_1.join();
    thread_2.join();
}

TEST_CASE("extension_test", "[extension_test]")
{
    auto client_function = []() { return EBPF_ERROR_SUCCESS; };
    auto provider_function = []() { return EBPF_ERROR_SUCCESS; };
    auto provider_attach = [](void* context,
                              const GUID* client_id,
                              void* client_binding_context,
                              const ebpf_extension_data_t* client_data,
                              const ebpf_extension_dispatch_table_t* client_dispatch_table) {
        UNREFERENCED_PARAMETER(context);
        UNREFERENCED_PARAMETER(client_id);
        UNREFERENCED_PARAMETER(client_data);
        UNREFERENCED_PARAMETER(client_dispatch_table);
        UNREFERENCED_PARAMETER(client_binding_context);
        return EBPF_ERROR_SUCCESS;
    };
    auto provider_detach = [](void* context, const GUID* client_id) {
        UNREFERENCED_PARAMETER(context);
        UNREFERENCED_PARAMETER(client_id);
        return EBPF_ERROR_SUCCESS;
    };
    ebpf_extension_dispatch_table_t client_dispatch_table = {
        0, sizeof(ebpf_extension_dispatch_table_t), client_function};
    ebpf_extension_dispatch_table_t provider_dispatch_table = {
        0, sizeof(ebpf_extension_dispatch_table_t), provider_function};
    ebpf_extension_data_t client_data;
    ebpf_extension_data_t provider_data;
    GUID interface_id;

    ebpf_extension_dispatch_table_t* returned_provider_dispatch_table;
    ebpf_extension_data_t* returned_provider_data;

    ebpf_extension_provider_t* provider_context;
    ebpf_extension_client_t* client_context;
    void* provider_binding_context;

    ebpf_guid_create(&interface_id);

    REQUIRE(
        ebpf_provider_load(
            &provider_context,
            &interface_id,
            nullptr,
            &provider_data,
            &provider_dispatch_table,
            nullptr,
            provider_attach,
            provider_detach) == EBPF_ERROR_SUCCESS);

    REQUIRE(
        ebpf_extension_load(
            &client_context,
            &interface_id,
            nullptr,
            &client_data,
            &client_dispatch_table,
            &provider_binding_context,
            &returned_provider_data,
            &returned_provider_dispatch_table) == EBPF_ERROR_SUCCESS);

    REQUIRE(returned_provider_data == &provider_data);
    REQUIRE(returned_provider_dispatch_table == &provider_dispatch_table);

    ebpf_extension_unload(client_context);
    epbf_provider_unload(provider_context);
}