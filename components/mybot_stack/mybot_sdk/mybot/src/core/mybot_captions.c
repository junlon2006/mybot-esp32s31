/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_captions.h"
#include "mybot_json.h"
#include <hal/aosl_hal_memory.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define CAPTION_ID_CAPACITY 64U
#define CAPTION_JSON_MAX_DEPTH 16U

typedef struct {
    bool has_time;
    uint64_t end_ms;
} caption_side_t;

typedef struct {
    mybot_caption_t value;
    char stream_id[CAPTION_ID_CAPACITY];
    char turn_id[CAPTION_ID_CAPACITY];
    caption_side_t source;
    caption_side_t target;
    bool source_received;
} caption_entry_t;

struct mybot_captions {
    mybot_caption_state_t state;
    uint32_t first_slot;
    caption_entry_t entries[MYBOT_CAPTION_HISTORY_CAPACITY];
};

static void advance_revision(mybot_captions_t *captions) {
    if (++captions->state.revision == 0)
        ++captions->state.revision;
}

mybot_captions_t *mybot_captions_create(void) {
    mybot_captions_t *captions = aosl_hal_malloc(sizeof(*captions));
    if (captions)
        memset(captions, 0, sizeof(*captions));
    return captions;
}

void mybot_captions_destroy(mybot_captions_t *captions) {
    aosl_hal_free(captions);
}

void mybot_captions_reset(mybot_captions_t *captions, uint32_t generation) {
    if (!captions)
        return;
    captions->state.generation = generation;
    captions->state.count = 0;
    captions->state.first_sequence = 0;
    captions->state.latest_sequence = 0;
    captions->first_slot = 0;
    advance_revision(captions);
}

void mybot_captions_get_state(const mybot_captions_t *captions, mybot_caption_state_t *out) {
    if (!out)
        return;
    if (captions)
        *out = captions->state;
    else
        memset(out, 0, sizeof(*out));
}

int mybot_captions_copy(const mybot_captions_t *captions, uint32_t sequence, mybot_caption_t *out) {
    if (!captions || !out || !sequence || sequence < captions->state.first_sequence ||
        sequence > captions->state.latest_sequence)
        return -1;
    uint32_t offset = sequence - captions->state.first_sequence;
    if (offset >= captions->state.count)
        return -1;
    *out =
        captions->entries[(captions->first_slot + offset) % MYBOT_CAPTION_HISTORY_CAPACITY].value;
    return 0;
}

/* Reject malformed UTF-8 before the JSON parser copies raw string bytes. */
static bool valid_utf8(const unsigned char *bytes, size_t length) {
    for (size_t i = 0; i < length;) {
        unsigned char first = bytes[i++];
        if (first < 0x80)
            continue;
        unsigned trailing;
        unsigned char lower = 0x80, upper = 0xbf;
        if (first >= 0xc2 && first <= 0xdf) {
            trailing = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            trailing = 2;
            if (first == 0xe0)
                lower = 0xa0;
            if (first == 0xed)
                upper = 0x9f;
        } else if (first >= 0xf0 && first <= 0xf4) {
            trailing = 3;
            if (first == 0xf0)
                lower = 0x90;
            if (first == 0xf4)
                upper = 0x8f;
        } else {
            return false;
        }
        if (trailing > length - i || bytes[i] < lower || bytes[i] > upper)
            return false;
        ++i;
        while (--trailing)
            if (bytes[i] < 0x80 || bytes[i++] > 0xbf)
                return false;
    }
    return true;
}

static bool bounded_nesting(const unsigned char *bytes, size_t length) {
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char current = bytes[i];
        if (!current)
            return false;
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (current == '\\')
                escaped = true;
            else if (current == '"')
                quoted = false;
        } else if (current == '"') {
            quoted = true;
        } else if (current == '{' || current == '[') {
            if (++depth > CAPTION_JSON_MAX_DEPTH)
                return false;
        } else if (current == '}' || current == ']') {
            if (!depth)
                return false;
            --depth;
        }
    }
    return !quoted && !depth;
}

/* Protocol keys are case-sensitive. Reject duplicate protocol fields rather
 * than depending on a JSON library's first/last occurrence convention. */
static const mybot_json_t *field(const mybot_json_t *root, const char *key, bool *valid) {
    const mybot_json_t *found = NULL;
    for (const mybot_json_t *item = root->child; item; item = item->next) {
        if (!item->string || strcmp(item->string, key))
            continue;
        if (found)
            *valid = false;
        found = item;
    }
    return found;
}

static bool integer(const mybot_json_t *item, int64_t *out) {
    return mybot_json_get_integer(item, out) && item->valuedouble == (double)*out;
}

static bool read_id(const mybot_json_t *item, char out[CAPTION_ID_CAPACITY]) {
    const char *text = mybot_json_get_string(item);
    if (text) {
        size_t length = strlen(text);
        if (!length || length >= CAPTION_ID_CAPACITY)
            return false;
        memcpy(out, text, length + 1);
        return true;
    }
    int64_t number;
    if (!integer(item, &number) || number < 0)
        return false;
    (void)snprintf(out, CAPTION_ID_CAPACITY, "%" PRId64, number);
    return true;
}

static bool read_time(const mybot_json_t *root, caption_side_t *side, bool *valid) {
    const mybot_json_t *start = field(root, "start_ms", valid);
    const mybot_json_t *duration = field(root, "duration_ms", valid);
    int64_t start_ms = 0, duration_ms = 0;
    if ((start && (!integer(start, &start_ms) || start_ms < 0)) ||
        (duration && (!integer(duration, &duration_ms) || duration_ms < 0)))
        return false;
    side->has_time = start && duration;
    side->end_ms = (uint64_t)start_ms + (uint64_t)duration_ms;
    return true;
}

static bool copy_text(char out[MYBOT_CAPTION_TEXT_CAPACITY], bool *truncated, const char *text) {
    size_t length = strlen(text);
    bool cut = length >= MYBOT_CAPTION_TEXT_CAPACITY;
    if (cut) {
        length = MYBOT_CAPTION_TEXT_CAPACITY - 1;
        while (((unsigned char)text[length] & 0xc0) == 0x80)
            --length;
    }
    bool changed = *truncated != cut || strlen(out) != length || memcmp(out, text, length);
    if (changed) {
        memcpy(out, text, length);
        out[length] = '\0';
        *truncated = cut;
    }
    return changed;
}

static caption_entry_t *find_entry(mybot_captions_t *captions, const char *stream,
                                   const char *turn) {
    for (uint32_t i = 0; i < captions->state.count; ++i) {
        caption_entry_t *entry =
            &captions->entries[(captions->first_slot + i) % MYBOT_CAPTION_HISTORY_CAPACITY];
        if (!strcmp(entry->stream_id, stream) && !strcmp(entry->turn_id, turn))
            return entry;
    }
    return NULL;
}

static caption_entry_t *append_entry(mybot_captions_t *captions, const char *stream,
                                     const char *turn) {
    if (captions->state.latest_sequence == UINT32_MAX)
        mybot_captions_reset(captions, captions->state.generation);
    if (captions->state.count == MYBOT_CAPTION_HISTORY_CAPACITY) {
        captions->first_slot = (captions->first_slot + 1) % MYBOT_CAPTION_HISTORY_CAPACITY;
        ++captions->state.first_sequence;
        --captions->state.count;
    }
    caption_entry_t *entry = &captions->entries[(captions->first_slot + captions->state.count) %
                                                MYBOT_CAPTION_HISTORY_CAPACITY];
    memset(entry, 0, sizeof(*entry));
    strcpy(entry->stream_id, stream);
    strcpy(entry->turn_id, turn);
    entry->value.sequence = ++captions->state.latest_sequence;
    if (!captions->state.count)
        captions->state.first_sequence = entry->value.sequence;
    ++captions->state.count;
    return entry;
}

static int apply_event(mybot_captions_t *captions, const mybot_json_t *root) {
    if (root->type != MYBOT_JSON_OBJECT)
        return -1;
    bool valid = true;
    const char *object = mybot_json_get_string(field(root, "object", &valid));
    if (!object || !valid)
        return -1;
    bool translation = !strcmp(object, "user.translation");
    if (!translation && strcmp(object, "user.transcription"))
        return 0;

    char stream[CAPTION_ID_CAPACITY], turn[CAPTION_ID_CAPACITY];
    const char *text = mybot_json_get_string(field(root, "text", &valid));
    if (!read_id(field(root, "stream_id", &valid), stream) ||
        !read_id(field(root, "turn_id", &valid), turn) || !text)
        return -1;
    const mybot_json_t *final_field = field(root, "final", &valid);
    if (final_field && final_field->type != MYBOT_JSON_FALSE &&
        final_field->type != MYBOT_JSON_TRUE)
        return -1;
    bool final = final_field && final_field->type == MYBOT_JSON_TRUE;
    caption_side_t incoming;
    if (!read_time(root, &incoming, &valid))
        return -1;
    const mybot_json_t *fallback_field =
        translation ? field(root, "transcript_text", &valid) : NULL;
    const char *fallback = mybot_json_get_string(fallback_field);
    if ((fallback_field && !fallback) || !valid)
        return -1;

    caption_entry_t *entry = find_entry(captions, stream, turn);
    bool changed = !entry;
    if (!entry)
        entry = append_entry(captions, stream, turn);
    caption_side_t *side = translation ? &entry->target : &entry->source;
    bool *stored_final = translation ? &entry->value.target_final : &entry->value.source_final;
    if ((*stored_final && !final) ||
        (side->has_time && incoming.has_time && incoming.end_ms < side->end_ms))
        return 0;
    if (incoming.has_time)
        *side = incoming;
    if (*stored_final != final) {
        *stored_final = final;
        changed = true;
    }
    if (translation) {
        changed |= copy_text(entry->value.target_text, &entry->value.target_truncated, text);
        if (fallback && !entry->source_received)
            changed |=
                copy_text(entry->value.source_text, &entry->value.source_truncated, fallback);
    } else {
        entry->source_received = true;
        changed |= copy_text(entry->value.source_text, &entry->value.source_truncated, text);
    }
    if (changed)
        advance_revision(captions);
    return changed ? 1 : 0;
}

int mybot_captions_apply_json(mybot_captions_t *captions, const void *data, size_t len) {
    if (!captions || !data || !len || len > MYBOT_CAPTION_MESSAGE_CAPACITY)
        return -1;
    const unsigned char *bytes = data;
    if (!bytes[len - 1])
        --len;
    if (!len || !valid_utf8(bytes, len) || !bounded_nesting(bytes, len))
        return -1;
    char *terminated = aosl_hal_malloc(len + 1);
    if (!terminated)
        return -1;
    memcpy(terminated, bytes, len);
    terminated[len] = '\0';
    mybot_json_t *root = mybot_json_parse(terminated);
    aosl_hal_free(terminated);
    if (!root)
        return -1;
    int result = apply_event(captions, root);
    mybot_json_delete(root);
    return result;
}
