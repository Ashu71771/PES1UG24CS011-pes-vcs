// ─── TODO: Implement these ───────────────────────────────────────────────────

// Load the index from .pes/index.
int index_load(Index *idx) {
    FILE *fp = fopen(".pes/index", "r");

    if (!fp) {
        idx->count = 0;
        return 0;
    }

    idx->count = 0;

    while (1) {
        IndexEntry *e = &idx->entries[idx->count];
        char hash_hex[65];

        int ret = fscanf(fp, "%o %64s %ld %ld %s\n",
                         &e->mode,
                         hash_hex,
                         &e->mtime_sec,
                         &e->size,
                         e->path);

        if (ret != 5) break;

        hex_to_hash(hash_hex, &e->oid);
        idx->count++;
    }

    fclose(fp);
    return 0;
}

// Save the index to .pes/index atomically.
int index_save(const Index *index) {
    FILE *fp = fopen(".pes/index.tmp", "w");
    if (!fp) return -1;

    for (int i = 0; i < index->count; i++) {
        char hash_hex[65];
        hash_to_hex(&index->entries[i].oid, hash_hex);

        fprintf(fp, "%o %s %ld %ld %s\n",
                index->entries[i].mode,
                hash_hex,
                index->entries[i].mtime_sec,
                index->entries[i].size,
                index->entries[i].path);
    }

    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);

    if (rename(".pes/index.tmp", ".pes/index") != 0) {
        return -1;
    }

    return 0;
}

// Stage a file for the next commit.
int index_add(Index *index, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "error: cannot open file '%s'\n", path);
        return -1;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    // Read file content
    unsigned char *data = malloc(size);
    if (!data) {
        fclose(fp);
        return -1;
    }

    fread(data, 1, size, fp);
    fclose(fp);

    // Write blob object
    ObjectID oid;
    if (object_write(OBJ_BLOB, data, size, &oid) != 0) {
        free(data);
        return -1;
    }

    // Get file metadata
    struct stat st;
    if (stat(path, &st) != 0) {
        free(data);
        return -1;
    }

    // Find existing entry
    IndexEntry *e = index_find(index, path);

    if (!e) {
        e = &index->entries[index->count++];
    }

    e->mode = 100644;
    e->oid = oid;
    e->size = st.st_size;
    e->mtime_sec = st.st_mtime;
    strcpy(e->path, path);

    free(data);

    return index_save(index);
}
