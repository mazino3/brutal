#include <brutal/alloc.h>
#include <brutal/codec/tga.h>
#include <brutal/gfx.h>
#include <brutal/log.h>
#include <elf/exec.h>
#include <handover/handover.h>
#include <proto/fs_dev.h>
#include <proto/vfs.h>
#include <syscalls/exec.h>
#include <syscalls/helpers.h>
#include <syscalls/shared_mem.h>
#include <syscalls/syscalls.h>

static void display_bootimage(Handover const *handover)
{
    // Open the framebuffer

    HandoverFramebuffer const *fb = &handover->framebuffer;

    size_t fb_size = fb->height * fb->pitch;

    BrCreateArgs fb_obj = {
        .type = BR_OBJECT_MEMORY,
        .mem_obj = {
            .addr = fb->addr,
            .size = fb_size,
            .flags = BR_MEM_OBJ_PMM,
        },
    };

    assert_br_success(br_create(&fb_obj));

    BrMapArgs fb_map = {
        .space = BR_SPACE_SELF,
        .mem_obj = fb_obj.handle,
        .flags = BR_MEM_WRITABLE,
    };

    assert_br_success(br_map(&fb_map));

    GfxSurface fb_surface = {
        .width = fb->width,
        .height = fb->height,
        .pitch = fb->pitch,
        .format = GFX_PIXEL_FORMAT_BGRA8888,
        .buffer = (void *)fb_map.vaddr,
        .size = fb_size,
    };

    // Open the bootimage

    HandoverModule const *img = handover_find_module(handover, str$("bootimg"));
    assert_not_null(img);

    BrCreateArgs img_obj = {
        .type = BR_OBJECT_MEMORY,
        .mem_obj = {
            .addr = img->addr,
            .size = img->size,
            .flags = BR_MEM_OBJ_PMM,
        },
    };

    assert_br_success(br_create(&img_obj));

    BrMapArgs img_map = {
        .space = BR_SPACE_SELF,
        .mem_obj = img_obj.handle,
        .flags = BR_MEM_WRITABLE,
    };

    assert_br_success(br_map(&img_map));

    GfxSurface img_surface = tga_decode_in_memory((void *)img_map.vaddr, img_map.size);

    // Display the image

    gfx_surface_clear(fb_surface, GFX_COLOR_BLACK);

    gfx_rast_fill(
        fb_surface,
        (Edgesf){
            4,
            (Edgef[]){
                {0, 0, 32, 0},
                {32, 0, 32, 32},
                {32, 32, 0, 0},
            },
        },
        (Recti){0, 0, 1024, 1024},
        (GfxPaint){.type = GFX_PAINT_FILL, .fill = GFX_COLOR_BLUE},
        alloc_global());

    gfx_surface_copy(
        fb_surface,
        img_surface,
        fb_surface.width / 2 - img_surface.width / 2,
        fb_surface.height / 2 - img_surface.height / 2);

    // Cleanup

    brh_close(img_obj.handle);
    brh_close(fb_obj.handle);

    assert_br_success(br_unmap(&(BrUnmapArgs){
        .space = BR_SPACE_SELF,
        .vaddr = fb_map.vaddr,
        .size = fb_map.size,
    }));

    assert_br_success(br_unmap(&(BrUnmapArgs){
        .space = BR_SPACE_SELF,
        .vaddr = img_map.vaddr,
        .size = img_map.size,
    }));
}

BrResult srv_run(Handover const *handover, Str name, BrExecArgs const *args, BrTaskInfos *infos)
{
    HandoverModule const *elf = handover_find_module(handover, name);

    assert_not_null(elf);

    BrCreateArgs elf_obj = {
        .type = BR_OBJECT_MEMORY,
        .mem_obj = {
            .addr = elf->addr,
            .size = elf->size,
            .flags = BR_MEM_OBJ_PMM,
        },
    };

    assert_br_success(br_create(&elf_obj));

    BrResult result = br_exec(elf_obj.handle, name, args, infos);

    brh_close(elf_obj.handle);

    log$("Service '{}' created!", name);

    return result;
}
static void init_vfs(Handover const *handover)
{
    // this is not 100% finalized, the majority of the code in this function is just for testing purpose

    BrTaskInfos vfs_server = {};
    srv_run(
        handover,
        str$("vfs"),
        &(BrExecArgs){
            .type = BR_START_ARGS,
        },
        &vfs_server);

    BrTaskInfos bootfs_server = {};
    srv_run(
        handover,
        str$("bootfs"),
        &(BrExecArgs){
            .type = BR_START_ARGS,
        },
        &bootfs_server);

    // mount bootfs

    Str path_mount = str$("/");
    SharedMem m = UNWRAP(shared_mem_from_str(path_mount));

    VfsMountRequest req =
        {
            .path = m.obj,
            .fshandle = bootfs_server.tid,
        };

    VfsMountResponse resp = {};

    vfs_mount(vfs_server.tid, &req, &resp);
    shared_mem_free(&m);

    // open file

    Str path_str = str$("/hello/path");
    log$("trying to create file: {}", path_str);
    SharedMem path_mem = UNWRAP(shared_mem_from_str(path_str));

    VfsOpenRequest open_req =
        {
            .path = path_mem.obj,
            .flags = 0,
        };

    log$("path memobj: {}", path_mem.obj);
    VfsOpenResponse open_resp =
        {

        };

    log$("try to create file");
    if (vfs_open(vfs_server.tid, &open_req, &open_resp) != VFS_SUCCESS)
    {
        return;
    }

    shared_mem_free(&path_mem);
    log$("trying to write file");

    // writing
    Str hello_data = str$("hello world");
    SharedMem write = UNWRAP(shared_mem_from_str(hello_data));

    VfsWriteRequest write_req =
        {
            .handle = open_resp.handle,
            .data = write.obj,
            .off = 0,
        };

    VfsWriteResponse write_resp;

    if (vfs_write(vfs_server.tid, &write_req, &write_resp) != VFS_SUCCESS)
    {
        return;
    }
    shared_mem_free(&write);
}

int br_entry_handover(Handover *handover)
{
    log$("Handover at {#p}", (void *)handover);

    handover_dump(handover);

    if (handover->framebuffer.present)
    {
        display_bootimage(handover);
    }

    BrTaskInfos posix_server = {};
    srv_run(
        handover,
        str$("posix"),
        &(BrExecArgs){
            .type = BR_START_CMAIN,
            .cmain = {
                .argc = 5,
                .argv = (Str[]){
                    str$("Hello"),
                    str$("world"),
                    str$("my"),
                    str$("friend"),
                    str$(":^)"),
                },
            },
        },
        &posix_server);

    BrTaskInfos acpi_server = {};
    srv_run(
        handover,
        str$("acpi"),
        &(BrExecArgs){
            .type = BR_START_HANDOVER,
            .handover = handover,
        },
        &acpi_server);

    BrTaskInfos pci_server = {};
    srv_run(
        handover,
        str$("pci"),
        &(BrExecArgs){
            .type = BR_START_HANDOVER,
            .handover = handover,
        },
        &pci_server);

    BrTaskInfos ps2_server = {};
    srv_run(
        handover,
        str$("ps2"),
        &(BrExecArgs){
            .type = BR_START_ARGS,
        },
        &ps2_server);

    init_vfs(handover);
    /*
    BrTaskInfos echo_server = {};
    srv_run(
        handover,
        str$("echo"),
        &(BrExecArgs){
            .type = BR_START_ARGS,
        },
        &echo_server);
        */

    while (true)
    {
        br_ipc(&(BrIpcArgs){
            .flags = BR_IPC_RECV | BR_IPC_BLOCK,
            .deadline = BR_DEADLINE_INFINITY,
        });
    }

    return 0;
}
