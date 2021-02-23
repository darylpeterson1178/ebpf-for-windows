/*
 *  Copyright (c) Microsoft Corporation
 *  SPDX-License-Identifier: MIT
*/
#pragma once

typedef enum _ebpf_operation_id {
    EBPF_OPERATION_EVIDENCE, 
    EBPF_OPERATION_RESOLVE_HELPER,
    EBPF_OPERATION_RESOLVE_MAP,
    EBPF_OPERATION_LOAD_CODE,
    EBPF_OPERATION_UNLOAD_CODE,
    EBPF_OPERATION_ATTACH_CODE,
    EBPF_OPERATION_DETACH_CODE,
    EBPF_OPERATION_CREATE_MAP,
    EBPF_OPERATION_MAP_LOOKUP_ELEMENT,
    EBPF_OPERATION_MAP_UPDATE_ELEMENT,
    EBPF_OPERATION_MAP_DELETE_ELEMENT
} ebpf_operation_id_t;

typedef struct _ebpf_map_definition {
    uint32_t size;
    uint32_t type;
    uint32_t key_size;
    uint32_t value_size;
    uint32_t max_entries;
} ebpf_map_definition_t;

typedef struct _ebpf_operation_header {
    uint16_t length;
    ebpf_operation_id_t id;
} ebpf_operation_header_t;

typedef struct _ebpf_operation_eidence_request {
    struct _ebpf_operation_header header;
    uint8_t EBPF_OPERATION_EVIDENCE[1];
} ebpf_operation_eidence_request_t;

typedef struct _ebpf_operation_evidence_reply {
    struct _ebpf_operation_header header;
    uint32_t status;
} ebpf_operation_evidence_reply_t;

typedef struct _ebpf_operation_resolve_helper_request {
    struct _ebpf_operation_header header;
    uint32_t helper_id[1];
} ebpf_operation_resolve_helper_request_t;

typedef struct _ebpf_operation_resolve_helper_reply {
    struct _ebpf_operation_header header;
    uint64_t address[1];
} ebpf_operation_resolve_helper_reply_t;

typedef struct _ebpf_operation_resolve_map_request {
    struct _ebpf_operation_header header;
    uint64_t map_handle[1];
} ebpf_operation_resolve_map_request_t;

typedef struct _ebpf_operation_resolve_map_reply {
    struct _ebpf_operation_header header;
    uint64_t address[1];
} ebpf_operation_resolve_map_reply_t;

typedef struct _ebpf_operation_load_code_request {
    struct _ebpf_operation_header header;
    uint8_t machine_code[1];
} ebpf_operation_load_code_request_t;

typedef struct _ebpf_operation_unload_code_request {
    struct _ebpf_operation_header header;
    uint64_t handle;
} ebpf_operation_unload_code_request_t;

typedef struct _ebpf_operation_load_code_reply {
    struct _ebpf_operation_header header;
    uint64_t handle;
} ebpf_operation_load_code_reply_t;

typedef struct _ebpf_operation_attach_detach_request {
    struct _ebpf_operation_header header;
    uint64_t handle;
    uint32_t hook;
} ebpf_operation_attach_detach_request_t;

typedef struct _ebpf_operation_create_map_request {
    struct _ebpf_operation_header header;
    struct _ebpf_map_definition ebpf_map_definition;
} ebpf_operation_create_map_request_t;

typedef struct _ebpf_operation_create_map_reply {
    struct _ebpf_operation_header header;
    uint64_t handle;
} ebpf_operation_create_map_reply_t;

typedef struct _ebpf_operation_map_lookup_element_request {
    struct _ebpf_operation_header header;
    uint64_t handle;
    uint8_t key[1];
} ebpf_operation_map_lookup_element_request;

typedef struct _ebpf_operation_map_lookup_element_reply {
    struct _ebpf_operation_header header;
    uint8_t value[1];
} ebpf_operation_map_lookup_element_reply_t;

typedef struct _ebpf_operation_map_update_element_request {
    struct _ebpf_operation_header header;
    uint64_t handle;
    uint8_t data[1]; // data is key+value
} epf_operation_map_update_element_request_;

typedef struct _ebpf_operation_map_delete_element_request {
    struct _ebpf_operation_header header;
    uint64_t handle;
    uint8_t key[1];
} ebpf_operation_map_delete_element_request_t;