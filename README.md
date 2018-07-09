# usb-toolbox
A simple tool for testing USB devices. The program allows you to send USB messages (URBs) to a USB device directly from a shell, using a set of concise commands. It's basically a REPL for libUSB. Developed and tested for Raspbian Stretch on a Raspberry Pi.

# Building
This program depends on the libUSB library. You can install it from `apt-get`:

    sudo apt-get install libusb-dev

You can then compile the program by starting make in the folder that contains the usb-toolbox source:

    make

# Running
Simply start the program as root. It doesn't take any parameters.

    sudo ./usb-toolbox
    
This will print a short list of commands and start a command prompt.     

# Functionality
Please note this program really only provides very basic functionality. To be exact, it provides all the functionality I needed a tool for. However, it should be easily possible to adapt the code for your needs or add commands for the libUSB functionality that isn't yet supported. If you added functionality, please open a pull request!

# Usage
This tool is best used together with a program for capturing USB messages such as Wireshark. This combination allows you to watch and record USB communication from the device you want to investigate and replay select messages. The format of the send-ctrl command in particular is very similar to the URB view in Wireshark, so you can copy the values without e.g. converting them to a different base. 

The core concept of the program is that you open a USB device, which you can then manipulate. Before being able to open another device, close the previously opened device. 

## Commands:
The available commands can be separated into two groups: The first group doesn't require an opened device for operation, while the second group operates only on the currently open device. Most commands can be invoked using either a full-length descriptive command name or a short two-letter abbreviated form.

#### list / l
Lists all available USB devices. This is essentially the same as the lsusb command, except it provides less information.

#### help / h
Displays a short help listing available commands.

#### exit
Exits the program. All open devices or hub ports are closed.

#### open {device_idx}
Opens the device that was shown at the {device_idx}th position in the list of available devices of the list command.

#### open {vendor_id} {product_id}
Opens the device matching the provided vendor id and product id. Vendor and Product id can be provided either as a decimal number or as a hex string prefixed with 0x.

#### close
Closes the currently open device.

#### reset
Resets the currently open device.

#### get-conf / gc
Retrieves the current USB configuration (bConfigurationValue) of the opened USB device.

#### change-conf / cc {b_config_value}
Sets the USB configuration of the opened USB device. Please note there currently is no way to list the available USB configurations, so it's recommended to use the `lsusb` command for that.

#### send-ctrl / sc {bmRequestType} {bRequest} {wValue} {wIndex} [wLength] [data] [timeout]
Sends a USB control message to the opened USB device. bmRequestType, bRequest, wValue and wIndex are mandatory fields. They  show up similarly labelled in the USB specifications and USB capture software, so I won't describe their meaning here.
The wLength parameter tells the program how many bytes it should allocate for the data buffer. The buffer content is either sent to the USB device or received from the device depending on the transfer direction, which is inferred from the request type. If a data parameter is specified, it is put into the allocated buffer. The timeout parameter specifies how long you want to wait for a response message from the USB device to arrive.
All parameters except data can be specified as either a decimal value or a hex value (use the 0x prefix). Data is either a string (starting and ending with double quotes `"`) or the hex representation of binary data (no prefix is needed in that case).

#### info
Displays details about the currently opened device. These details are useful for the claim command. In particular, the info of a hub specifies the bus and address you have to use to claim one of its ports.

#### claim {bus} {hub} {port}
Claim a port of a USB hub. Claiming a port prevents the operating system from automatically configuring USB devices that are connected to it. This allows you to manually configure the device without intervention from the operating system.
bus is the id of the bus the hub is attached to. hub is the address of the hub. port is the port number (starting from 1) on that hub.

#### unclaim
Unclaim the previously claimed hub port.
