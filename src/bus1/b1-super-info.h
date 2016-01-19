#pragma once
/***
  This file is part of bus1. See COPYING for details.

  bus1 is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  bus1 is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with bus1; If not, see <http://www.gnu.org/licenses/>.
***/

 /*

  Generic header used to identify raw images. The binary UUID acts as
  a magic to identify the metadata block.

  The type UUID identifies the type of the data in the image. The
  tag acts as a human-readable identifier, should contain only
  these characters [a-zA-Z0-9-._].

  The object UUID identifies the instance of an object, it is newly
  allocated, every time the header is used. The label is the human-readable
  identifier and should be a valid UTF-8 string.

  Example #1 fancyfs:
    967e8b2d-db0c-4c8b-b753-16aa2492b42b  Super Header UUID
    a8562cf4-2297-4cf1-bdf6-3903811ff22f  FancyFS UUID
    fancyfs                               Filesystem type as tag
    d0b9e022-7543-4d4c-82ed-9700a1a21fa4  This FancyFS Filesystem
    Images                                Human readable lable

  Example #2 Application Image:
    967e8b2d-db0c-4c8b-b753-16aa2492b42b  Super Header UUID
    b746c4f5-c3c4-4737-8a4c-54bee475692a  Bus1 Image Format
    org.bus1.image
    80f2a24c-0bf4-442d-9148-d60f52876d87  This Application Image
    org.example.application               Name of the application

  This allows probing tools to identify a raw image and prepare it for further
  handling. No type-specific knowledge about the format is needed to identify
  the basic properties of an image. Filesystem probing and mounting tools do
  not need to be updated for future filesystem or image formats.

  The header is always 368 bytes long. It is usually placed at the start of the
  image right before the type-specific metadata begins. For optional metadata
  appended to the image, the header might be placed at the end of the image.

  */

#define BUS1_SUPER_INFO_UUID    { 0x96, 0x7e, 0x8b, 0x2d, 0xdb, 0x0c, 0x4c, 0x8b, 0xb7, 0x53, 0x16, 0xaa, 0x24, 0x92, 0xb4, 0x2b }

struct Bus1SuperHeader {
        uint8_t super_uuid[16];                 /* indentifier of a Super Header (mandatory) */

        uint8_t type_uuid[16];                  /* Type of data using the Super Header (mandatory) */
        char type_tag[64];                      /* Human-readable type string */

        uint8_t object_uuid[16];                /* Identifier for the object using the Super header */
        char object_label[256];                 /* Human readable label for this object */
};

typedef struct Bus1SuperHeader Bus1SuperHeader;
