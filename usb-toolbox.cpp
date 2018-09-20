#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <exception>
#include <memory>
#include <iomanip>
#include <algorithm>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>

#include <libusb-1.0/libusb.h>

#define USBDEVFS_CLAIM_PORT        _IOR('U', 24, unsigned int)
#define USBDEVFS_RELEASE_PORT      _IOR('U', 25, unsigned int)

#define DEFAULT_TIMEOUT 5000
#define MAX_DATA_SIZE 0xFFFF

const std::string help_string =
    "\n"
    "usb-toolbox (c) 2018 Florian Draschbacher \n"
    "A simple tool for testing USB devices \n"
    "\n"
    "Available commands are: \n"
    "list / l: List all connected usb devices \n"
    "open {device_idx}: Open a device \n"
    "open {vendor_id} {product_id}: Open a device\n"
    "get-conf / gc: Get the opened device's configuration \n"
    "change-conf / cc {b_config_value}: set the opened device's configuration \n"
    "send-ctrl / sc {bmRequestType} {bRequest} {wValue} {wIndex} [wLength] [data] [timeout]: \n"
    "            Send a control URB to the opened device \n"
    "reset: Reset the opened device \n"
    "close: Close the opened device \n"
    "info / i: Get details about the opened device \n"
    "claim {bus} {hub} {port}: Claim a hub's port \n"
    "unclaim: Release claimed hub port \n"
    "help / h: Displays this help \n"
    "exit: Stop usb-toolbox \n"
    "";

// Utility functions
template<typename T2>
inline T2 parse_number(const std::string &string) {
    unsigned long int out;
    std::stringstream ss;
    if (string.length() >= 2 && string[0] == '0' && string[1] == 'x') ss << std::hex << string;
    else ss << string;
    ss >> out;
    return (T2) out;
}

std::vector<unsigned char> parse_data(std::string data_string, size_t size){
    if (data_string[0] == '"'){
        size = std::min(size, data_string.size() - 2);
        return std::vector<unsigned char>(data_string.begin() + 1, data_string.begin() + size);
    } else {
        size = std::min(size, data_string.size() / 2);
        std::vector<unsigned char> data(size);
        for (unsigned int i = 0; i < size; i++) {
            std::string byteString = data_string.substr(i * 2, 2);
            data[i] = (char) strtol(byteString.c_str(), NULL, 16);
        }
        return data;
    }
}

void print_data(std::vector<unsigned char> data){
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < data.size(); ++i){
        if (i % 16 == 0) ss << std::endl;
        else if (i % 8 == 0) ss << "  ";
        else ss << " ";
        ss << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    std::cout << ss.str() << std::endl;
}

// Global variables
libusb_context *usb_ctx = nullptr; // The libusb session
libusb_device_handle *opened_device = nullptr; // The currently opened device or nullptr
int claimed_fd = -1;
int claimed_port = -1;

// Function prototypes
void list_devices();
void open_device(int device_idx);
void open_device(uint16_t vendor_id, uint16_t product_id);
void reset_device();
void close_device();
void get_device_configuration();
void set_device_configuration(int b_config_value);
void send_device_control(uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, 
    uint16_t wLength, std::vector<unsigned char> data, unsigned int timeout);
void claim_hub_port(unsigned int bus, unsigned int hub, unsigned int port);
void unclaim_hub_port();
void get_device_info();

int main() {
    // Set up libusb
    int error = libusb_init(&usb_ctx);
    if (error < 0) {
        std::cout << "Could not initialize libusb session: " << libusb_error_name(error) << std::endl;
        return -1;
    }
    libusb_set_debug(usb_ctx, 3);

    std::cout << help_string << std::endl;

    while (1){
        std::cout << "> ";

        std::string line;
        getline(std::cin, line);
        
        // Split line into tokens
        std::vector<std::string> tokens;
        size_t pos = 0;
        do {
            pos = line.find(" ");
            tokens.push_back(line.substr(0, pos == std::string::npos ? line.length() : pos));
            line.erase(0, pos + 1);
        } while (pos != std::string::npos);

        if (tokens.size() == 0) continue;
        std::string command = tokens[0];

        try {
            if (command == "exit") {
                // Unclaim hub port
                unclaim_hub_port();
                // Close device if open
                close_device();
                // Close libusb session
                libusb_exit(usb_ctx);
                return 0;

            } else if (command == "list" || command == "l" || command == "ls") {
                list_devices();

            } else if (command == "open") {
                if (tokens.size() < 2) {
                    std::cout << "Too few arguments" << std::endl;
                    continue;
                } else if (tokens.size() >= 3) open_device(parse_number<uint16_t>(tokens[1]), parse_number<uint16_t>(tokens[2]));
                else if (tokens.size() == 2) open_device(parse_number<unsigned int>(tokens[1]));

            } else if (command == "close") {
                close_device();

            } else if (command == "reset") {
                reset_device();

            } else if (command == "info") {
                get_device_info();

            } else if (command == "get-conf" || command == "gc") {
                get_device_configuration();

            } else if (command == "change-conf" || command == "cc") {
                if (tokens.size() < 2) {
                    std::cout << "Too few arguments" << std::endl;
                    continue;
                }
                set_device_configuration(parse_number<unsigned int>(tokens[1]));

            } else if (command == "send-ctrl" || command == "sc") {
                if (tokens.size() < 5) {
                    std::cout << "Too few arguments" << std::endl;
                    continue;
                }
                uint8_t bmRequestType = parse_number<uint8_t>(tokens[1]);
                uint8_t bRequest = parse_number<uint8_t>(tokens[2]);
                uint16_t wValue = parse_number<uint16_t>(tokens[3]);
                uint16_t wIndex = parse_number<uint16_t>(tokens[4]);

                std::vector<unsigned char> data;
                uint16_t wLength = 0;
                unsigned int timeout = DEFAULT_TIMEOUT;

                if (tokens.size() >= 6) wLength = parse_number<uint16_t>(tokens[5]);
                if (tokens.size() >= 7) {
                    tokens[6].erase(std::remove(tokens[6].begin(), tokens[6].end(), ' '), tokens[6].end());
                    data = parse_data(tokens[5], wLength);   
                    print_data(data);
                }
                if (tokens.size() >= 8) timeout = parse_number<unsigned int>(tokens[7]);

                send_device_control(bmRequestType, bRequest, wValue, wIndex, wLength, data, timeout);
            
            } else if (command == "claim") {
                if (tokens.size() < 4) {
                    std::cout << "Too few arguments" << std::endl;
                    continue;
                }

                claim_hub_port(parse_number<unsigned int>(tokens[1]), parse_number<unsigned int>(tokens[2]), 
                    parse_number<unsigned int>(tokens[3]));

            } else if (command == "unclaim") {
                unclaim_hub_port();

            } else if (command == "help" || command == "h") {
                std::cout << help_string << std::endl;

            } else {
                std::cout << "Unsupported command: " << command << std::endl;
            }
        } catch (std::exception &exception) {
            std::cout << "Could not execute command: " << exception.what() << std::endl;
        } catch (...) {
            std::cout << "Could not execute command: Unknown Error" << std::endl;
        }
    }

    return 0;
}

void list_devices() {
    libusb_device **devices;
    ssize_t count = libusb_get_device_list(usb_ctx, &devices);
    if (count < 0) {
        std::cout << "Could not get devices: " << libusb_error_name(count) << count << std::endl;
        return;
    }

    std::cout << "Devices Found: " << count << std::endl;
    
    for (unsigned int i = 0; i < count; i++) {
        libusb_device_descriptor device;
        int error = libusb_get_device_descriptor(devices[i], &device);
        if (error < 0) {
            std::cout << "Could not get device #" << i << ": " << libusb_error_name(error) << std::endl;
            continue;
        }

        std::cout << std::endl << "DEVICE " << i << ":" << std::endl;

        std::cout << "Available Configurations:" << (int)device.bNumConfigurations << std::endl;
        std::cout << "Device Class: " << (int)device.bDeviceClass << std::endl;
        std::cout << "Vendor: 0x" << std::hex << device.idVendor << std::endl;
        std::cout << "Product: 0x" << std::hex << device.idProduct << std::endl;
    }

    libusb_free_device_list(devices, 1);
}

void open_device(int device_idx) {
    if (opened_device != nullptr) {
        std::cout << "Closing currently opened device" << std::endl;
        close_device();
    }

    libusb_device **devices;
    ssize_t count = libusb_get_device_list(usb_ctx, &devices);
    if (count < 0) {
        std::cout << "Could not get devices: " << libusb_error_name(count) << std::endl;
        return;
    }
 
    if (device_idx < 0 || device_idx >= count) {
        std::cout << "Device index out of range." << std::endl;
        libusb_free_device_list(devices, 1);
        return;
    }

    int error = libusb_open(devices[device_idx], &opened_device);
    if (error < 0) {
        std::cout << "Could not open device: " << libusb_error_name(error) << std::endl;
    }

    libusb_free_device_list(devices, 1);
}

void open_device(uint16_t vendor_id, uint16_t product_id) {
    if (opened_device != nullptr){
        std::cout << "Closing currently opened device" << std::endl;
        close_device();
    }

    opened_device = libusb_open_device_with_vid_pid(usb_ctx, vendor_id, product_id);
    if (opened_device == NULL) {
        std::cout << "Could not open device: Returned NULL" << std::endl;
    }
}

void reset_device() {
    if (opened_device == nullptr) {
        std::cout << "Open a device first." << std::endl;
        return;
    }

    int error = libusb_reset_device(opened_device);
    if (error == LIBUSB_ERROR_NOT_FOUND) {
        std::cout << "Lost opened device while resetting." << std::endl;
        close_device();
    } else if (error < 0) {
        std::cout << "Could not reset device: " << libusb_error_name(error) << std::endl;
    }
}

void close_device(){
    if (opened_device == nullptr) {
        std::cout << "There isn't any device currently opened." << std::endl;
        return;
    }

    libusb_close(opened_device);
    opened_device = nullptr;
}

void get_device_configuration() {
    if (opened_device == nullptr) {
        std::cout << "Open a device first." << std::endl;
        return;
    }

    int b_config;
    int error = libusb_get_configuration(opened_device, &b_config);
    if (error < 0) {
        std::cout << "Could not get active configuration: " << libusb_error_name(error) << std::endl;
        return;
    }

    std::cout << "Currently active: bConfigurationValue " << b_config << std::endl;
}

void set_device_configuration(int b_config_value) {
    if (opened_device == nullptr) {
        std::cout << "Open a device first." << std::endl;
        return;
    }

    int error = libusb_set_configuration(opened_device, b_config_value);
    if (error < 0) {
        std::cout << "Could not set configuration: " << libusb_error_name(error) << std::endl;
        return;
    }
}

void send_device_control(uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, 
    uint16_t wLength, std::vector<unsigned char> data, unsigned int timeout) {
    
    if (opened_device == nullptr) {
        std::cout << "Open a device first." << std::endl;
        return;
    }

    if ((bmRequestType & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
        data.resize(wLength);
    }

    int error = libusb_control_transfer(opened_device, bmRequestType, bRequest, wValue, wIndex, 
        data.data(), wLength, timeout);

    if (error < 0) {
        std::cout << "Could not send control packet: " << libusb_error_name(error) << std::endl;
        return;
    }

    if ((bmRequestType & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
        std::cout << "Received response data: ";
        print_data(data);
    }
}

void claim_hub_port(unsigned int bus, unsigned int hub, unsigned int port){
    if (claimed_fd >= 0) {
        std::cout << "Unclaiming previously claimed hub port" << std::endl;
        unclaim_hub_port();
    }

    std::stringstream sfilename;
    sfilename << "/dev/bus/usb/" << std::setfill('0') << std::setw(3) << bus 
        << "/" << std::setfill('0') << std::setw(3) << hub;

    std::cout << "Claiming " << sfilename.str() << " port " << port << std::endl;

    claimed_fd = open(sfilename.str().c_str(), O_WRONLY);
    if (claimed_fd < 0) {
        std::cout << "Could not open hub device file: Error " << claimed_fd << std::endl;
        claimed_fd = -1;
        return;
    }

    int error = ioctl(claimed_fd, USBDEVFS_CLAIM_PORT, &port);
    if (error < 0) {
        std::cout << "Could not claim port: Error " << error << std::endl;
        close(claimed_fd);
        claimed_fd = -1;
        return;
    }

    claimed_port = port;
}

void unclaim_hub_port(){
    if (claimed_fd < 0) {
        std::cout << "There isn't any hub port currently claimed." << std::endl;
        return;
    }

    int error = ioctl(claimed_fd, USBDEVFS_RELEASE_PORT, &claimed_port);
    if (error < 0) {
        std::cout << "Could not unclaim port: Error " << error << std::endl;
    }

    close(claimed_fd);
    claimed_fd = -1;
}

void get_device_info(){
    if (opened_device == nullptr) {
        std::cout << "Open a device first." << std::endl;
        return;
    }

    libusb_device *device = libusb_get_device(opened_device);

    std::cout << "Opened Device:" << std::endl;
    std::cout << "Bus: " << std::setfill('0') << std::setw(3) << (int)libusb_get_bus_number(device) << std::endl;
    std::cout << "Port: " << std::setfill('0') << std::setw(3) << (int)libusb_get_port_number(device) << std::endl;
    std::cout << "Address: " << std::setfill('0') << std::setw(3) << (int)libusb_get_device_address(device) << std::endl;

    device = libusb_get_parent(device);
    std::cout << "Parent: " << std::endl;
    std::cout << "Bus: " <<  std::setfill('0') << std::setw(3) << (int)libusb_get_bus_number(device) << std::endl;
    std::cout << "Port: " <<  std::setfill('0') << std::setw(3) << (int)libusb_get_port_number(device) << std::endl;
    std::cout << "Address: " <<  std::setfill('0') << std::setw(3) << (int)libusb_get_device_address(device) << std::endl;
}
