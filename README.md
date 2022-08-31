# jsfw

Utility to forward uevent devices over network through a tcp connection.

# Usage

Start client:

```sh
jsfw client [server address] [server port]
```

Start server:

```sh
jsfw server [port]
```

When a device is connected to the server host, jsfw will notice it and assign it to one of the client which will in turn create a virtual device based on it.

The code can theoretically support any kind of device (mouse, keyboard, joystick...) but is artificially limited to PS4 controllers (see `hid.c::filter_event`), because the hidraw interface used to set additional device state (led color, flashing, rumble) only works with them. This could be easily edited tho (see `hid.c::apply_controller_state`, `net.h::MessageControllerState`, `net.c::{msg_serialize, msg_deserialize}` and `client.c::JControllerState`). To set the controller state from the client write the json state to the fifo (by default `/tmp/jsfw_fifo`).

The format for the controller state takes this form (comments not allowed):

```json
{
    "led_color": "#ff0000", // hex color string
    "flash": [0.04, 0.11], // values are 0-1, first is time on second is time off
    "rumble": [0, 0], // values are 0-1
}
```

Any value can be ommitted, extra values will be ignored.

Some aspect are easily configurable through `const.c`.

# Building

## Dependencies

None apart from libc, libmath, pthreads, and linux headers.

## Compiling

Use the makefile, or open it and do things by hand, there's nothing magic in there.

```sh
make jsfw
```

output will be `./jsfw`.
