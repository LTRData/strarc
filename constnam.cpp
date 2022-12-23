/* Stream Archive I/O utility, Copyright (C) Olof Lagerkvist 2004-2022
*
* constnam.cpp
* Human readable names of backup-related constants.
*/

#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _DLL
#define _DLL
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NON_CONFORMING_WCSTOK
#define _CRT_NON_CONFORMING_WCSTOK
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Use the WinStructured library classes and functions.
#include <windows.h>

#include <stdio.h>

const char *stream_ids[] = {
    "BACKUP_INVALID",        // 0x00000000 Header (not valid backup stream) 
    "BACKUP_DATA",           // 0x00000001 Standard data 
    "BACKUP_EA_DATA",        // 0x00000002 Extended attribute data 
    "BACKUP_SECURITY_DATA",  // 0x00000003 Security descriptor data 
    "BACKUP_ALTERNATE_DATA", // 0x00000004 Alternative data streams 
    "BACKUP_LINK",           // 0x00000005 Hard link information 
    "BACKUP_PROPERTY_DATA",  // 0x00000006 Property data 
    "BACKUP_OBJECT_ID",      // 0x00000007 Objects identifiers 
    "BACKUP_REPARSE_DATA",   // 0x00000008 Reparse points 
    "BACKUP_SPARSE_BLOCK",   // 0x00000009 Sparse file. 
    "BACKUP_TXFS_DATA"       // 0x0000000a TXFS stream
};

char stream_id_unknown[12];

LPCSTR
GetStreamIdDescription(DWORD StreamId)
{
    if (StreamId >= (sizeof(stream_ids) / sizeof(*stream_ids)))
    {
        _snprintf(stream_id_unknown, sizeof(stream_id_unknown),
            "%#x", StreamId);
        stream_id_unknown[sizeof(stream_id_unknown) - 1] = 0;
        return stream_id_unknown;
    }

    return stream_ids[StreamId];
}

const char *attrib_ids[] = {
    "STREAM_NORMAL_ATTRIBUTE", //         0x00000000
    "STREAM_MODIFIED_WHEN_READ", //       0x00000001
    "STREAM_CONTAINS_SECURITY", //        0x00000002
    "STREAM_CONTAINS_PROPERTIES", //      0x00000004
    "STREAM_SPARSE_ATTRIBUTE" //          0x00000008
};

char attrib_id_list[260];

LPCSTR
GetStreamAttributesDescription(DWORD StreamAttributesId)
{
    if (StreamAttributesId == 0)
        return attrib_ids[0];

    attrib_id_list[0] = 0;

    for (int i = 1;
        (i < (sizeof(attrib_ids) / sizeof(*attrib_ids))) &
        (StreamAttributesId != 0);
    i++, StreamAttributesId >>= 1)
        if (StreamAttributesId & 1)
        {
            if (attrib_id_list[0] != 0)
            {
                strncat(attrib_id_list, " | ",
                    sizeof(attrib_id_list) - strlen(attrib_id_list));
                attrib_id_list[sizeof(attrib_id_list) - 1] = 0;
            }

            strncat(attrib_id_list, attrib_ids[i],
                sizeof(attrib_id_list) - strlen(attrib_id_list));
            attrib_id_list[sizeof(attrib_id_list) - 1] = 0;
        }

    return attrib_id_list;
}

const char *file_attrib_ids[] = {
    "(none)",                             // 0x00000000  
    "FILE_ATTRIBUTE_READONLY",            // 0x00000001  
    "FILE_ATTRIBUTE_HIDDEN",              // 0x00000002  
    "FILE_ATTRIBUTE_SYSTEM",              // 0x00000004  
    "FILE_ATTRIBUTE_00000008",            // 0x00000008  
    "FILE_ATTRIBUTE_DIRECTORY",           // 0x00000010  
    "FILE_ATTRIBUTE_ARCHIVE",             // 0x00000020  
    "FILE_ATTRIBUTE_DEVICE",              // 0x00000040  
    "FILE_ATTRIBUTE_NORMAL",              // 0x00000080  
    "FILE_ATTRIBUTE_TEMPORARY",           // 0x00000100  
    "FILE_ATTRIBUTE_SPARSE_FILE",         // 0x00000200  
    "FILE_ATTRIBUTE_REPARSE_POINT",       // 0x00000400  
    "FILE_ATTRIBUTE_COMPRESSED",          // 0x00000800  
    "FILE_ATTRIBUTE_OFFLINE",             // 0x00001000  
    "FILE_ATTRIBUTE_NOT_CONTENT_INDEXED", // 0x00002000  
    "FILE_ATTRIBUTE_ENCRYPTED",           // 0x00004000  
    "FILE_ATTRIBUTE_00008000",            // 0x00008000  
    "FILE_ATTRIBUTE_VIRTUAL"              // 0x00010000  
};

char file_attrib_id_list[260];

LPCSTR
GetFileAttributesDescription(DWORD FileAttributes)
{
    if (FileAttributes == 0)
        return file_attrib_ids[0];

    file_attrib_id_list[0] = 0;

    for (int i = 1;
        (i < (sizeof(file_attrib_ids) / sizeof(*file_attrib_ids))) &&
        (FileAttributes != 0); i++, FileAttributes >>= 1)
    {
        if (FileAttributes & 1)
        {
            if (file_attrib_id_list[0] != 0)
            {
                strncat(file_attrib_id_list, " | ",
                    sizeof(file_attrib_id_list) - strlen(file_attrib_id_list));
                file_attrib_id_list[sizeof(file_attrib_id_list) - 1] = 0;
            }

            strncat(file_attrib_id_list,
                file_attrib_ids[i],
                sizeof(file_attrib_id_list) - strlen(file_attrib_id_list));
            file_attrib_id_list[sizeof(file_attrib_id_list) - 1] = 0;
        }
    }

    return file_attrib_id_list;
}
