# ESP32 OTA ASYM

This project is about asymmetric over the air update partitioning for the ESP32

## The Problem

The offered well functioning and bullet prove OTA mechanism uses 2 equally sized partitions 
`ota_0` and `ota_1` which are used in turn. While one is active, the space of the other partition
is used to store the updated data.

This cuts the flash memory you can use for your code in half, which blocks some implementations.
If you for example use BLE you know what I talk about. 

## The Idea

Using asymmetric partitioning you can use one - smaller - partition to only hold the logic to 
(locally) copy over the firmware to the other partition and use the larger partition to hold
your full application, and the logic to pull the new firmware from somewhere.  

If your project has a SD card connected, as the one I'm working with, you have plenty of storage space
available to store the data locally.

## The Implementation

There is no implementation yet but the idea. Plan is to use as much of the existing functionality 
as possible to keep all ESP32 OTA functionality alive and be able to use existing tools as much as
possible.

The code for the larger partition should be as little as possible so that is can be easily called and
integrated into existing applications.

The code for the smaller partition should be on one side very generic but also as compact as possible
to allow a small partition size.

## details

SPI_FLASH_DANGEROUS_WRITE - must be set to allow

Partition table after the update is binary stored in `partition_table` you
can see the content in `main/sd-partition-table.csv` it results in the following:

    # Name,   Type, SubType, Offset,  Size, Flags
    nvs,      data, nvs,     0x009000, 0x005000,
    otadata,  data, ota,     0x00e000, 0x002000,
    app,      app,  ota_0,   0x010000, 0x380000,
    flashApp, app,  ota_1,   0x390000, 0x040000,
    spiffs,   data, spiffs,  0x3D0000, 0x030000,

It is mainly based on the existing partition schema, to change as little as possible
and let existing `spiffs` data intact.