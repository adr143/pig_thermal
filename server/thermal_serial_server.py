"""Read newline-delimited ESP32 thermal frames from a USB serial port."""
from __future__ import annotations

import logging
import threading
from typing import Optional

import serial
from serial import SerialException

from config import config
from live_state import state
from thermal_ws_server import handle_thermal_message

log = logging.getLogger("thermal.serial")
_reader_thread: Optional[threading.Thread] = None
_reader_lock = threading.Lock()


def start_thermal_serial_reader(stop_event: threading.Event) -> None:
    """Start one reconnecting serial reader when a port is configured."""
    global _reader_thread
    if not config.THERMAL_SERIAL_PORT:
        log.info("thermal serial reader disabled: THERMAL_SERIAL_PORT is empty")
        return

    with _reader_lock:
        if _reader_thread is not None and _reader_thread.is_alive():
            return
        _reader_thread = threading.Thread(
            target=_read_serial_frames,
            args=(stop_event,),
            name="thermal-serial-reader",
            daemon=True,
        )
        _reader_thread.start()


def _read_serial_frames(stop_event: threading.Event) -> None:
    while not stop_event.is_set():
        try:
            with serial.Serial(
                config.THERMAL_SERIAL_PORT,
                baudrate=config.THERMAL_SERIAL_BAUD,
                timeout=1,
            ) as serial_port:
                clients = state.inc_thermal_client()
                log.info(
                    "thermal serial connected on %s at %d baud (clients=%d)",
                    config.THERMAL_SERIAL_PORT,
                    config.THERMAL_SERIAL_BAUD,
                    clients,
                )
                try:
                    while not stop_event.is_set():
                        raw_line = serial_port.readline()
                        if not raw_line:
                            continue
                        try:
                            message = raw_line.decode("utf-8").strip()
                        except UnicodeDecodeError:
                            log.warning("thermal serial: received non-UTF-8 data")
                            continue
                        if not message:
                            continue
                        if not message.startswith("{"):
                            log.debug("thermal serial: ignored device diagnostic")
                            continue
                        handle_thermal_message(message, source="serial/ESP32")
                finally:
                    clients = state.dec_thermal_client()
                    log.info("thermal serial disconnected (clients=%d)", clients)
        except SerialException:
            log.exception(
                "thermal serial: cannot read %s; retrying in 5 seconds",
                config.THERMAL_SERIAL_PORT,
            )
            stop_event.wait(5)
