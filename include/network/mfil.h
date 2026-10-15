#ifndef __MFIL_H__
#define __MFIL_H__

#include <downloader.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Server definition in master manifest (e.g., WoW.mfil) */
typedef struct {
    char                                                                    name[128];
    char                                                                    location[512];
} MfilServer;

/* Master manifest containing server locations and partial manifest references */
typedef struct {
    int                                                                     version;
    MfilServer                                                              *servers;
    size_t                                                                  server_count;
    char                                                                    manifest_partial[256];
    char                                                                    client_partial[256];
} MfilMaster;

/* Serverpath definition in partial manifest */
typedef struct {
    char                                                                    name[128];
    char                                                                    path[256];
} MfilServerPath;

/* File entry in partial manifest */
typedef struct {
    char                                                                    file[256];
    char                                                                    name[256];
    int64_t                                                                 size;
    char                                                                    checksum[64];
    int                                                                     fileversion;
    int                                                                     flags;
    char                                                                    path[128];
} MfilFile;

/* Partial manifest containing file lists and server paths */
typedef struct {
    int                                                                     version;
    MfilServerPath                                                          *serverpaths;
    size_t                                                                  serverpath_count;
    MfilFile                                                                *files;
    size_t                                                                  file_count;
} MfilManifest;

/* Parse a master manifest file (e.g., WoW.mfil) from disk */
bool __mfil_parse_master_file__(const char *filepath, MfilMaster *master);

/* Parse a master manifest from an in-memory string */
bool __mfil_parse_master_string__(const char *content, size_t length, MfilMaster *master);

/* Free resources allocated inside an MfilMaster */
void __mfil_free_master__(MfilMaster *master);

/* Parse a partial manifest file (e.g., wow-12340-*.mfil) from disk */
bool __mfil_parse_manifest_file__(const char *filepath, MfilManifest *manifest);

/* Parse a partial manifest from an in-memory string */
bool __mfil_parse_manifest_string__(const char *content, size_t length, MfilManifest *manifest);

/* Free resources allocated inside an MfilManifest */
void __mfil_free_manifest__(MfilManifest *manifest);

/* Get the primary server location from a master manifest, or NULL */
const char *__mfil_get_master_location__(const MfilMaster *master, const char *server_name);

/* Find the serverpath prefix for a given path key */
const char *__mfil_get_serverpath__(const MfilManifest *manifest, const char *path_key);

/* Build full download URL by joining base location, subpath, and filename */
bool __mfil_build_url__(const char *base_location, const char *subpath, const char *filename, char *out_url, size_t out_url_size);

/* Download a file specified in MfilFile using the downloader engine */
CURLStatus __mfil_download_file__(const char *base_location, const MfilManifest *manifest, const MfilFile *file, const char *destination_dir, const CURLOptions *options, CURLResult *result);

/* Download a partial manifest using the downloader engine */
CURLStatus __mfil_download_partial__(const char *base_location, const char *partial_filename, const char *destination_path, const CURLOptions *options, CURLResult *result);

/* Search a manifest for a file entry matching filename or basename (case-insensitive) */
const MfilFile *__mfil_find_file__(const MfilManifest *manifest, const char *filename);

/* Lookup the unmodified checksum for a client executable across known MFIL manifests */
bool __mfil_get_client_unmodified_checksum__(const char *client_filename, char out_checksum[64]);

/* Calculate MD5 checksum of a file, writing 32-hex chars + null terminator into out_md5 */
bool __mfil_calculate_file_checksum__(const char *filepath, char out_md5[33]);

/* Verify MD5 checksum of a downloaded file */
bool __mfil_verify_file_checksum__(const char *filepath, const char *expected_md5);

#endif // __MFIL_H__

