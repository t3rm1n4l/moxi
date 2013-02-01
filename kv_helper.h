typedef struct kvpair {
    char*  key;
    char** values;
    int    allocated_values;
    int    used_values;
    struct kvpair* next;
} kvpair_t;

void free_string_list(char **vals)
{   
    int i = 0;
    for (i = 0; vals[i]; i++) {
        free(vals[i]);
    }
    free(vals);
}   

void free_kvpair(kvpair_t* pair)
{
    if (pair) {
        free_kvpair(pair->next);
        free(pair->key);
        free_string_list(pair->values);
        free(pair);
    }
}

kvpair_t* find_kvpair(kvpair_t* pair, const char* key)
{
    assert(key);

    while (pair && strcmp(pair->key, key) != 0) {
        pair = pair->next;
    }   

    return pair;
}

