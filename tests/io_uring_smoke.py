#!/usr/bin/env python3

import os
import socket
import struct
import subprocess
import sys
import tempfile
import time


CMD_SET = 0x01
CMD_GET = 0x02
CMD_PING = 0x05
STATUS_FAILURE = 0x00
STATUS_SUCCESS = 0x01


def build_set(key: bytes, value: bytes) -> bytes:
    core = bytes([CMD_SET]) + struct.pack(">H", len(key)) + key
    core += struct.pack(">H", len(value)) + value
    return struct.pack(">H", len(core)) + core


def build_get(key: bytes) -> bytes:
    core = bytes([CMD_GET]) + struct.pack(">H", len(key)) + key
    return struct.pack(">H", len(core)) + core


def build_ping(value: bytes) -> bytes:
    core = bytes([CMD_PING]) + struct.pack(">H", len(value)) + value
    return struct.pack(">H", len(core)) + core


def read_exact(sock: socket.socket, size: int) -> bytes:
    chunks = []
    remaining = size
    while remaining > 0:
        chunk = sock.recv(remaining)
        if not chunk:
            raise AssertionError("connection closed while reading response")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def read_frame(sock: socket.socket) -> bytes:
    header = read_exact(sock, 2)
    (core_len,) = struct.unpack(">H", header)
    return read_exact(sock, core_len)


def expect_success_value(sock: socket.socket, expected: bytes) -> None:
    frame = read_frame(sock)
    assert frame[0] == STATUS_SUCCESS, frame
    assert len(frame) >= 3, frame
    (value_len,) = struct.unpack(">H", frame[1:3])
    assert frame[3:] == expected, frame
    assert value_len == len(expected), frame


def expect_ping(sock: socket.socket, expected: bytes) -> None:
    frame = read_frame(sock)
    assert frame[0] == CMD_PING, frame
    assert len(frame) >= 3, frame
    (value_len,) = struct.unpack(">H", frame[1:3])
    assert frame[3:] == expected, frame
    assert value_len == len(expected), frame


def expect_disconnect(sock: socket.socket) -> None:
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        try:
            data = sock.recv(1)
        except (BrokenPipeError, ConnectionResetError):
            return
        except socket.timeout:
            continue

        if data == b"":
            return

        raise AssertionError(f"unexpected response from malformed frame: {data!r}")

    raise AssertionError("server did not close malformed connection")


def unused_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait_for_server(proc: subprocess.Popen, port: int) -> None:
    deadline = time.monotonic() + 8
    last_error = None
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            stdout, stderr = proc.communicate(timeout=1)
            raise AssertionError(
                f"server exited early with {proc.returncode}\n"
                f"stdout={stdout.decode(errors='replace')}\n"
                f"stderr={stderr.decode(errors='replace')}"
            )
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.25):
                return
        except OSError as exc:
            last_error = exc
            time.sleep(0.05)
    raise AssertionError(f"server did not accept connections: {last_error}")


def dump_server_output(stdout: bytes, stderr: bytes) -> None:
    if stdout:
        print("server stdout:", file=sys.stderr)
        print(stdout.decode(errors="replace"), file=sys.stderr)
    if stderr:
        print("server stderr:", file=sys.stderr)
        print(stderr.decode(errors="replace"), file=sys.stderr)


def stop_server(proc: subprocess.Popen, print_output: bool = False) -> None:
    if proc.poll() is not None:
        stdout, stderr = proc.communicate(timeout=1)
        if print_output:
            dump_server_output(stdout, stderr)
        return

    proc.terminate()
    try:
        proc.wait(timeout=8)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)
        raise

    stdout, stderr = proc.communicate(timeout=1)
    if print_output:
        dump_server_output(stdout, stderr)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: io_uring_smoke.py /path/to/fkvs-server", file=sys.stderr)
        return 2

    server_path = sys.argv[1]
    port = unused_port()
    config = (
        f"port {port}\n"
        "bind 127.0.0.1\n"
        "max-clients 16\n"
        "event-loop-max-events 128\n"
        "show-logo false\n"
        "verbose false\n"
        "daemonize false\n"
        "use-io-uring true\n"
    )

    with tempfile.NamedTemporaryFile("w", delete=False) as cfg:
        cfg.write(config)
        cfg_path = cfg.name

    proc = subprocess.Popen(
        [server_path, "-c", cfg_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )

    failed = False
    try:
        wait_for_server(proc, port)

        with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
            sock.settimeout(2)
            sock.sendall(
                build_set(b"alpha", b"one")
                + build_get(b"alpha")
                + build_ping(b"probe")
            )
            expect_success_value(sock, b"one")
            expect_success_value(sock, b"one")
            expect_ping(sock, b"probe")

        with socket.create_connection(("127.0.0.1", port), timeout=2) as bad:
            bad.settimeout(2)
            try:
                bad.sendall(b"\xff\xff" + (b"\x01" * (65536 - 2)))
            except (BrokenPipeError, ConnectionResetError):
                pass
            expect_disconnect(bad)

        with socket.create_connection(("127.0.0.1", port), timeout=2) as sock:
            sock.settimeout(2)
            sock.sendall(build_get(b"alpha"))
            expect_success_value(sock, b"one")

    except Exception:
        failed = True
        raise
    finally:
        stop_server(proc, print_output=failed)
        os.unlink(cfg_path)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
