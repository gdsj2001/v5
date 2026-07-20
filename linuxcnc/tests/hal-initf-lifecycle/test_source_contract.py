#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path


LINUXCNC_ROOT = Path(__file__).resolve().parents[2]
HAL_LIB = LINUXCNC_ROOT / "src" / "hal" / "hal_lib.c"
HAL_API = LINUXCNC_ROOT / "src" / "hal" / "hal.h"


def section(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def validate(hal_lib: str, hal_api: str) -> None:
    initf = section(
        hal_lib,
        "int hal_init_funct_to_thread(",
        "\nint hal_del_funct_from_thread(",
    )
    duplicate_scan = initf.index("list_entry = list_next(list_root);")
    duplicate_match = initf.index("SHMPTR(funct_entry->funct_ptr) == funct")
    duplicate_reject = initf.index("return -EEXIST;")
    availability = initf.index("(funct->users > 0) && (funct->reentrant == 0)")
    allocation = initf.index("alloc_funct_entry_struct()")
    assert duplicate_scan < duplicate_match < duplicate_reject < availability < allocation
    assert "(funct->uses_fp) && (!thread->uses_fp)" in initf

    delf = section(
        hal_lib,
        "int hal_del_funct_from_thread(",
        "\nint hal_start_threads(",
    )
    cyclic_list = delf.index("list_root = &(thread->funct_list);")
    init_list = delf.index("list_root = &(thread->init_funct_list);")
    assert cyclic_list < init_list
    assert delf.count("free_funct_entry_struct(funct_entry);") == 2

    thread_task = section(
        hal_lib,
        "static void thread_task(void *arg)\n{",
        "\nstatic hal_thread_t *alloc_thread_struct(void)\n{",
    )
    special_cycle = section(
        thread_task,
        "if (hal_data->threads_running > 0 && !thread->init_done)",
        "} else if (hal_data->threads_running > 0)",
    )
    assert "thread->init_done = 1;" in special_cycle
    assert "list_remove_entry" not in special_cycle
    assert "free_funct_entry_struct" not in special_cycle

    free_thread = section(
        hal_lib,
        "static void free_thread_struct(hal_thread_t * thread)\n{",
        "\n#endif /* RTAPI */",
    )
    cyclic_list = free_thread.index("list_root = &(thread->funct_list);")
    init_list = free_thread.index("list_root = &(thread->init_funct_list);")
    assert cyclic_list < init_list
    assert free_thread.count("free_funct_entry_struct(funct_entry);") >= 2
    assert free_thread.index("thread->init_done = 0;") > init_list

    assert "same function twice" in hal_api
    assert "returns -EEXIST" in hal_api
    assert "failed initf configuration symmetrically removable" in hal_api


source = HAL_LIB.read_text(encoding="utf-8")
api = HAL_API.read_text(encoding="utf-8")
validate(source, api)

# Each mutation represents a specific regression: duplicate admission,
# non-reentrant sharing, RT-side free-list mutation, missing delf/thread
# cleanup, or a misleading public API.
mutations = (
    (source.replace("return -EEXIST;", "return 0;", 1), api),
    (source.replace("(funct->users > 0) && (funct->reentrant == 0)", "0", 2), api),
    (source.replace("thread->init_done = 1;", "free_funct_entry_struct(funct_entry);\n\t    thread->init_done = 1;", 1), api),
    (source.replace("list_root = &(thread->init_funct_list);", "list_root = &(thread->funct_list);"), api),
    (source, api.replace("returns -EEXIST", "is accepted", 1)),
)
for mutated_source, mutated_api in mutations:
    try:
        validate(mutated_source, mutated_api)
    except (AssertionError, ValueError):
        pass
    else:
        raise AssertionError("HAL initf lifecycle regression was accepted")

print("HAL_INITF_LIFECYCLE_SOURCE_CONTRACT_OK")
