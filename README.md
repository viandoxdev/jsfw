# jsfw

Utility to forward evdev devices over network through a tcp connection.

# Building

## Dependencies

None apart from libc, libmath, pthreads, and linux headers.

## Compiling

Use the makefile, or open it and do things by hand, there's nothing magic in there.

```sh
make jsfw
```

output will be `./jsfw`.

# Usage

## Background

Jsfw works with a server and one or more clients, each client has a configuration and so does the server. The server configuration assigns tags to devices based on a set of filter, for example, you could give the "Controller" tag to devices of a certain vendor and product id. The client configuration specifies what the client wants: it is a set of tags as well as a few additional properties for the virtual devices. To illustrate that a little better, a simple setup would have a server with this configuration:

```json
{
    "controllers": [
        {
            "filter": { "vendor": "1234", "product": "0000" },
            "tag": "Controller"
        }
    ],
}
```

and a client with this one:

```json 
{
    "slots": [
        {
            "controllers": [
                {
                    "tag": "Controller"
                }
            ]
        }
    ]
}
```

This would result in the server picking up any device with the vendor id `1234` and the product id `0000`, taging it as a `Controller` and forwarding it to the client when it connects.

## Configuration

Jsfw makes use of two json configurations to dictate the behaviour of both the server and client.

### Server

The server configuration specifies some of the server settings, as well as which devices to assign to which tag.

```js
// Any property can be ommitted, unless specified otherwise
// The values listed here are examples
{
    "controllers": [
        {
            // (required) The tag to assign the to devices matching the filter
            "tag": "Joystick",
            // (default: accept any device) Requirements for a device to be assigned the tag
            "filter": {
                // (default: none) Optionally match the uniq of a device, expects a 17 long string of this form
                "uniq": "aa:bb:cc:dd:ee:ff",
                // (default: none) Optionally match the vendor code of the device, expects a 4 long hex string
                "vendor": "054c",
                // (default: none) Optionally match the product code of the device, expects a 4 long hex string
                "product": "abcd",
                // (default: false) Wether to check for a js* entry in the device tree, useful to match only the
                // controller when a device has multiple events (i.e in the case of a ps4 controller one device has
                // a js and is the controller, and another, with the same uniq/vendor/product, is the mouse and keyboard).
                "js": true,
                // (default: none) Optionally match the name of the device
                "name": "Asus keyboard"
            },
            // Additional properties for the jsfw behaviour, some properties may act as a filter.
            "properties": {
                // (default: false) Wether this device can be shared by multiple client
                "duplicate": true,
                // (default: false) Wether the devices are dualshock 4 controllers that can be controlled
                // through the hidraw interface, this allows changing the led colors from the client by writing
                // json to the fifo (see client configuration). If this is enabled, any device whose hidraw interface
                // can't be found will be filtered out
                "ps4_hidraw": true
            }
        }
    ],
    // (default: 1s) Number of seconds between each poll for physical devices
    "poll_interval": 2.5,
    // (default: 2s) Number of seconds to wait for a client's request before closing the connection
    "request_timeout": 10
}
```

The client configuration specifies what devices the client wants as well as under what name/properties should they appear

```js
// Any property can be ommitted, unless specified otherwise
// The values listed here are examples
// Comments are here to document things, they are not allowed in the actual config
{
    // (required) The slots the client has
    "slots": [
        {
            // (required) The controllers that are accepted in that slot
            "controllers": [
                {
                    // (required) Accepted tags of the device to request
                    "tag": ["Joystick"],
                    // (default: 6969) Vendor code for the virtual device, expects a 4 long hex string
                    "vendor": "dead",
                    // (default: 0420) Product code for the virtual device, expects a 4 long hex string
                    "product": "beef",
                    // (default: "JSFW Virtual Device") Name for the virtual device
                    "name": "Emanuel"
                }
            ]
        }
    ],
    // (default: "/tmp/jsfw_fifo") Path to the fifo for hidraw
    "fifo_path": "/tmp/gaming",
    // (default: 5s) Number of seconds between retries when connecting to the server
    "retry_delay": 2.5
}
```

Additionaly messages can be sent to the client's fifo to change the led colors (and more) of ps4\_hidraw devices, these take the form:

```js
// Any property can be ommitted, unless specified otherwise
// The values listed here are examples
{
    // (default: 0) Index of the slot to send the state to, this is the index in the client configuration controllers list
    "index": 1,
    // (default: [0, 0]) Setting for the rumble, values are in range 0-255 first element is small rumble, second is big
    "rumble": [255, 0],
    // (default: [0, 0]) Setting for led flash, values are in range 0-255, first element is led on, second is led off
    "flash": [0, 255],
    // (default: "#FFFFFF") Hex color for the led
    "led_color": "#FF0000"
}
```

## Execution

To start the server:

```sh
./jsfw server [port] [path to config]
```

To start the client:

```sh
./jsfw client [address] [port] [path to config]
```

# Contributing

lol, lmao even. (nah just issues/PR I guess)
