const std = @import("std");
const builtin = @import("builtin");

const required_zig_version = std.SemanticVersion{
    .major = 0,
    .minor = 15,
    .patch = 2,
};

const BuildOptions = struct {
    warnings: bool,
    warnings_as_errors: bool,
    sanitizers: bool,
};

const server_sources = [_][]const u8{
    "src/memory.c",
    "src/counter.c",
    "src/client.c",
    "src/core/list.c",
    "src/config.c",
    "src/networking/networking.c",
    "src/server.c",
    "src/server_lifecycle.c",
    "src/server_limits.c",
    "src/core/hashtable.c",
    "src/commands/common/command_registry.c",
    "src/commands/server/server_command_handlers.c",
    "src/ttl.c",
    "src/numeric_parse.c",
};

const benchmark_sources = [_][]const u8{
    "src/string_utils.c",
    "src/fkvs-benchmark.c",
    "src/client.c",
    "src/core/list.c",
    "src/networking/networking.c",
    "src/commands/common/command_parser.c",
    "src/commands/client/client_command_handlers.c",
};

const cli_sources = [_][]const u8{
    "src/string_utils.c",
    "src/fkvs-cli.c",
    "src/config.c",
    "src/commands/common/command_parser.c",
    "src/commands/client/client_command_handlers.c",
};

const TestTarget = struct {
    name: []const u8,
    sources: []const []const u8,
    server_defines: bool = false,
};

const test_counter_sources = [_][]const u8{
    "tests/test_counter.c",
    "src/counter.c",
};

const test_string_utils_sources = [_][]const u8{
    "tests/test_string_utils.c",
    "src/string_utils.c",
};

const test_hashtable_sources = [_][]const u8{
    "tests/test_hashtable.c",
    "src/core/hashtable.c",
};

const test_response_writer_sources = [_][]const u8{
    "tests/test_response_writer.c",
    "src/client.c",
    "src/commands/common/command_registry.c",
};

const test_server_lifecycle_sources = [_][]const u8{
    "tests/test_server_lifecycle.c",
    "src/server_lifecycle.c",
    "src/client.c",
    "src/core/list.c",
    "src/core/hashtable.c",
};

const test_server_config_sources = [_][]const u8{
    "tests/test_server_config.c",
    "src/config.c",
    "src/numeric_parse.c",
};

const test_server_limits_sources = [_][]const u8{
    "tests/test_server_limits.c",
    "src/server_limits.c",
};

const test_integration_sources = [_][]const u8{
    "tests/test_integration.c",
    "src/client.c",
    "src/core/hashtable.c",
    "src/commands/common/command_registry.c",
    "src/commands/common/command_parser.c",
    "src/commands/server/server_command_handlers.c",
    "src/counter.c",
    "src/ttl.c",
    "src/numeric_parse.c",
};

const test_targets = [_]TestTarget{
    .{ .name = "test_counter", .sources = &test_counter_sources },
    .{ .name = "test_string_utils", .sources = &test_string_utils_sources },
    .{ .name = "test_hashtable", .sources = &test_hashtable_sources },
    .{ .name = "test_response_writer", .sources = &test_response_writer_sources },
    .{ .name = "test_server_lifecycle", .sources = &test_server_lifecycle_sources },
    .{
        .name = "test_server_config",
        .sources = &test_server_config_sources,
        .server_defines = true,
    },
    .{ .name = "test_server_limits", .sources = &test_server_limits_sources },
    .{
        .name = "test_integration",
        .sources = &test_integration_sources,
        .server_defines = true,
    },
};

pub fn build(b: *std.Build) void {
    if (builtin.zig_version.order(required_zig_version) != .eq) {
        @panic("fkvs Zig build requires Zig 0.15.2");
    }

    const target = b.standardTargetOptions(.{
        .default_target = defaultTargetQuery(),
    });
    const optimize = b.standardOptimizeOption(.{});

    const options = BuildOptions{
        .warnings = b.option(bool, "warnings", "Enable strict C compiler warnings") orelse true,
        .warnings_as_errors = b.option(bool, "warnings-as-errors", "Treat C warnings as errors") orelse false,
        .sanitizers = b.option(bool, "sanitizers", "Enable C address and undefined-behavior sanitizers") orelse false,
    };
    const enable_io_uring = b.option(
        bool,
        "io-uring",
        "Build the Linux server with io_uring instead of epoll and link liburing",
    ) orelse false;

    const app_c_flags = cFlags(b, options, false);
    const test_c_flags = cFlags(b, options, true);
    const linenoise = linenoiseLibrary(b, target, optimize, app_c_flags);

    const server = cExecutable(b, .{
        .name = "fkvs-server",
        .target = target,
        .optimize = optimize,
        .options = options,
        .sources = &server_sources,
        .flags = app_c_flags,
    });
    server.root_module.addCMacro("SERVER", "1");
    addServerDispatcher(b, server.root_module, target, enable_io_uring, app_c_flags);
    b.installArtifact(server);

    const benchmark = cExecutable(b, .{
        .name = "fkvs-benchmark",
        .target = target,
        .optimize = optimize,
        .options = options,
        .sources = &benchmark_sources,
        .flags = app_c_flags,
    });
    benchmark.root_module.addCMacro("CLI", "1");
    benchmark.root_module.linkSystemLibrary("pthread", .{});
    b.installArtifact(benchmark);

    const cli = cExecutable(b, .{
        .name = "fkvs-cli",
        .target = target,
        .optimize = optimize,
        .options = options,
        .sources = &cli_sources,
        .flags = app_c_flags,
    });
    cli.root_module.addCMacro("CLI", "1");
    cli.root_module.linkLibrary(linenoise);
    b.installArtifact(cli);

    const test_step = b.step("test", "Build and run C test executables");
    for (test_targets) |spec| {
        const test_exe = cExecutable(b, .{
            .name = spec.name,
            .target = target,
            .optimize = optimize,
            .options = options,
            .sources = spec.sources,
            .flags = test_c_flags,
        });
        if (spec.server_defines) {
            test_exe.root_module.addCMacro("SERVER", "1");
        }

        const run_test = b.addRunArtifact(test_exe);
        test_step.dependOn(&run_test.step);
    }
}

const CExecutableOptions = struct {
    name: []const u8,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    options: BuildOptions,
    sources: []const []const u8,
    flags: []const []const u8,
};

fn cExecutable(b: *std.Build, opts: CExecutableOptions) *std.Build.Step.Compile {
    const mod = b.createModule(.{
        .target = opts.target,
        .optimize = opts.optimize,
        .link_libc = true,
        .sanitize_c = if (opts.options.sanitizers) .full else null,
        .omit_frame_pointer = if (opts.options.sanitizers) false else null,
    });
    mod.addCMacro("_POSIX_C_SOURCE", "200809L");
    mod.addIncludePath(b.path("src"));
    mod.addIncludePath(b.path("deps/linenoise"));
    mod.addCSourceFiles(.{
        .files = opts.sources,
        .flags = opts.flags,
        .language = .c,
    });

    return b.addExecutable(.{
        .name = opts.name,
        .root_module = mod,
    });
}

fn linenoiseLibrary(
    b: *std.Build,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    flags: []const []const u8,
) *std.Build.Step.Compile {
    const mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    mod.addIncludePath(b.path("deps/linenoise"));
    mod.addCSourceFile(.{
        .file = b.path("deps/linenoise/linenoise.c"),
        .flags = flags,
        .language = .c,
    });

    return b.addLibrary(.{
        .name = "linenoise",
        .root_module = mod,
    });
}

fn addServerDispatcher(
    b: *std.Build,
    mod: *std.Build.Module,
    target: std.Build.ResolvedTarget,
    enable_io_uring: bool,
    flags: []const []const u8,
) void {
    const dispatcher_source = switch (target.result.os.tag) {
        .macos => "src/io/event_dispatcher_kqueue.c",
        .linux => blk: {
            if (enable_io_uring) {
                mod.addCMacro("IO_URING_ENABLED", "1");
                mod.linkSystemLibrary("uring", .{});
                break :blk "src/io/event_dispatcher_io_uring.c";
            }
            break :blk "src/io/event_dispatcher_epoll.c";
        },
        else => @panic("fkvs Zig build currently supports macOS and Linux targets"),
    };

    mod.addCSourceFile(.{
        .file = b.path(dispatcher_source),
        .flags = flags,
        .language = .c,
    });
}

fn cFlags(b: *std.Build, opts: BuildOptions, is_test: bool) []const []const u8 {
    var flags: std.ArrayList([]const u8) = .empty;

    appendFlag(b, &flags, "-std=c23");
    if (is_test) {
        appendFlag(b, &flags, "-UNDEBUG");
    }

    if (opts.warnings) {
        appendFlag(b, &flags, "-Wall");
        appendFlag(b, &flags, "-Wextra");
        appendFlag(b, &flags, "-Wpedantic");
        if (opts.warnings_as_errors) {
            appendFlag(b, &flags, "-Werror");
        }
    }

    if (opts.sanitizers) {
        appendFlag(b, &flags, "-fsanitize=address,undefined");
        appendFlag(b, &flags, "-fno-omit-frame-pointer");
    }

    return flags.toOwnedSlice(b.allocator) catch @panic("OOM");
}

fn appendFlag(
    b: *std.Build,
    flags: *std.ArrayList([]const u8),
    flag: []const u8,
) void {
    flags.append(b.allocator, flag) catch @panic("OOM");
}

fn defaultTargetQuery() std.Target.Query {
    return switch (builtin.target.os.tag) {
        .macos => .{
            .cpu_arch = builtin.cpu.arch,
            .os_tag = .macos,
        },
        else => .{},
    };
}
