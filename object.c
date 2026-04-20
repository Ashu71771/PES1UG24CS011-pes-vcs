// object.c — Content-addressable object store

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── IMPLEMENTATION ──────────────────────────────────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    char header[64];
    const char *type_str;

    if (type == OBJ_BLOB) type_str = "blob";
    else if (type == OBJ_TREE) type_str = "tree";
    else if (type == OBJ_COMMIT) type_str = "commit";
    else return -1;

    // Create header
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len) + 1;

    // Combine header + data
    size_t total_len = header_len + len;
    unsigned char *full = malloc(total_len);
    if (!full) return -1;

    memcpy(full, header, header_len);
    memcpy(full + header_len, data, len);

    // Compute hash
    compute_hash(full, total_len, id_out);

    // Check if object already exists
    if (object_exists(id_out)) {
        free(full);
        return 0;
    }

    // Create object path
    char path[512];
    object_path(id_out, path, sizeof(path));

    // Create directory
    char dir[512];
    strncpy(dir, path, sizeof(dir));
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        mkdir(dir, 0755);
    }

    // Temp file path
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    int fd = open(tmp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        free(full);
        return -1;
    }

    if (write(fd, full, total_len) != (ssize_t)total_len) {
        close(fd);
        free(full);
        return -1;
    }

    fsync(fd);
    close(fd);

    // Atomic rename
    if (rename(tmp_path, path) != 0) {
        free(full);
        return -1;
    }

    free(full);
    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    char path[512];
    object_path(id, path, sizeof(path));

    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    // Read full file
    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    rewind(fp);

    unsigned char *buf = malloc(size);
    if (!buf) {
        fclose(fp);
        return -1;
    }

    fread(buf, 1, size, fp);
    fclose(fp);

    // Verify hash
    ObjectID computed;
    compute_hash(buf, size, &computed);

    if (memcmp(computed.hash, id->hash, HASH_SIZE) != 0) {
        free(buf);
        return -1;
    }

    // Find null separator
    void *null_pos = memchr(buf, '\0', size);
    if (!null_pos) {
        free(buf);
        return -1;
    }

    size_t header_len = (unsigned char*)null_pos - buf;

    // Parse header
    char type_str[10];
    size_t data_size;

    sscanf((char*)buf, "%s %zu", type_str, &data_size);

    if (strcmp(type_str, "blob") == 0) *type_out = OBJ_BLOB;
    else if (strcmp(type_str, "tree") == 0) *type_out = OBJ_TREE;
    else if (strcmp(type_str, "commit") == 0) *type_out = OBJ_COMMIT;
    else {
        free(buf);
        return -1;
    }

    // Extract data
    unsigned char *data_start = (unsigned char*)null_pos + 1;

    *data_out = malloc(data_size);
    if (!*data_out) {
        free(buf);
        return -1;
    }

    memcpy(*data_out, data_start, data_size);
    *len_out = data_size;

    free(buf);
    return 0;
}
