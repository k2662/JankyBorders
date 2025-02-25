#include "windows.h"
#include "hashtable.h"
#include <string.h>
#include "border.h"

extern pid_t g_pid;

bool windows_window_create(struct table* windows, uint32_t wid, uint64_t sid) {
  bool window_created = false;
  int cid = SLSMainConnectionID();
  int wid_cid = 0;
  SLSGetWindowOwner(cid, wid, &wid_cid);

  pid_t pid = 0;
  SLSConnectionGetPID(wid_cid, &pid);
  if (pid == g_pid) return false;

  CFArrayRef target_ref = cfarray_of_cfnumbers(&wid,
                                               sizeof(uint32_t),
                                               1,
                                               kCFNumberSInt32Type);

  if (!target_ref) return false;

  CFTypeRef query = SLSWindowQueryWindows(cid, target_ref, 1);
  if (query) {
    CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
    if (iterator && SLSWindowIteratorGetCount(iterator) > 0) {
      if (SLSWindowIteratorAdvance(iterator)) {
        ITERATOR_WINDOW_SUITABLE(iterator, {
          struct border* border = table_find(windows, &wid);
          if (!border) {
            border = malloc(sizeof(struct border));
            border_init(border);
            table_add(windows, &wid, border);
            window_created = true;
          }

          border->target_wid = wid;
          border->sid = sid;
          border->needs_redraw = true;
          border_draw(border);
          windows_update_notifications(windows);
        });
      }
    }
    if (iterator) CFRelease(iterator);
    CFRelease(query);
  }
  CFRelease(target_ref);

  return window_created;
}

void windows_update_all(struct table* windows) {
  for (int window_index = 0; window_index < windows->capacity; ++window_index) {
    struct bucket* bucket = windows->buckets[window_index];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border) {
          border->needs_redraw = true;
          border_draw(border);
        }
      }
      bucket = bucket->next;
    }
  }
}

void windows_update_active(struct table* windows) {
  for (int window_index = 0; window_index < windows->capacity; ++window_index) {
    struct bucket* bucket = windows->buckets[window_index];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border && border->focused) {
          border->needs_redraw = true;
          border_draw(border);
        }
      }
      bucket = bucket->next;
    }
  }
}

void windows_update_inactive(struct table* windows) {
  for (int window_index = 0; window_index < windows->capacity; ++window_index) {
    struct bucket* bucket = windows->buckets[window_index];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border && !border->focused) {
          border->needs_redraw = true;
          border_draw(border);
        }
      }
      bucket = bucket->next;
    }
  }
}

void windows_window_update(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) border_draw(border);
}

bool windows_window_focus(struct table* windows, uint32_t wid) {
  bool found_window = false;
  for (int window_index = 0; window_index < windows->capacity; ++window_index) {
    struct bucket* bucket = windows->buckets[window_index];
    while (bucket) {
      if (bucket->value) {
        struct border* border = bucket->value;
        if (border->focused && border->target_wid != wid) {
          border->focused = false;
          border->needs_redraw = true;
          border_draw(border);
        }

        if (!border->focused && border->target_wid == wid) {
          border->focused = true;
          border->needs_redraw = true;
          border_draw(border);
        }

        if (border->target_wid == wid) found_window = true;
      }
      bucket = bucket->next;
    }
  }

  return found_window;
}

void windows_window_move(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) border_move(border);
}

void windows_window_hide(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) border_hide(border);
}

void windows_window_unhide(struct table* windows, uint32_t wid) {
  struct border* border = table_find(windows, &wid);
  if (border) border_unhide(border);
}

bool windows_window_destroy(struct table* windows, uint32_t wid, uint32_t sid) {
  struct border* border = table_find(windows, &wid);
  if (border && border->sid == sid) {
    table_remove(windows, &wid);
    border_destroy(border);
    windows_update_notifications(windows);
    return true;
  }
  return false;
}

void windows_update_notifications(struct table* windows) {
  int window_count = 0;
  uint32_t window_list[1024] = {};

  for (int window_index = 0; window_index < windows->capacity; ++window_index) {
    struct bucket *bucket = windows->buckets[window_index];
    while (bucket) {
      if (bucket->value) {
        uint32_t wid = *(uint32_t *) bucket->key;
        window_list[window_count++] = wid;
      }
      bucket = bucket->next;
    }
  }

  int cid = SLSMainConnectionID();
  SLSRequestNotificationsForWindows(cid, window_list, window_count);
}

void windows_draw_borders_on_current_spaces(struct table* windows) {
  debug("Space Change: Consistency check\n");
  int cid = SLSMainConnectionID();
  CFArrayRef displays = SLSCopyManagedDisplays(cid);
  uint32_t space_count = CFArrayGetCount(displays);
  uint64_t space_list[space_count];

  for (int i = 0; i < space_count; i++) {
    space_list[i] = SLSManagedDisplayGetCurrentSpace(cid,
                                          CFArrayGetValueAtIndex(displays, i));
  }

  CFRelease(displays);

  CFArrayRef space_list_ref = cfarray_of_cfnumbers(space_list,
                                                   sizeof(uint64_t),
                                                   space_count,
                                                   kCFNumberSInt64Type);

  uint64_t set_tags = 1;
  uint64_t clear_tags = 0;
  CFArrayRef window_list = SLSCopyWindowsWithOptionsAndTags(cid,
                                                            0,
                                                            space_list_ref,
                                                            0x2,
                                                            &set_tags,
                                                            &clear_tags    );

  if (window_list) {
    uint32_t window_count = CFArrayGetCount(window_list);
    CFTypeRef query = SLSWindowQueryWindows(cid, window_list, window_count);
    if (query) {
      CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);
      if (iterator) {
        while(SLSWindowIteratorAdvance(iterator)) {
          ITERATOR_WINDOW_SUITABLE(iterator, {
            uint32_t wid = SLSWindowIteratorGetWindowID(iterator);
            struct border* border = table_find(windows, &wid);
            if (border) border_draw(border);
            else {
              debug("Creating Missing Window: %d\n", wid);
              windows_window_create(windows, wid, window_space_id(cid, wid));
            }
          });
        }
      }
      CFRelease(query);
    }
    CFRelease(window_list);
  }

  CFRelease(space_list_ref);
}

void windows_add_existing_windows(struct table* windows) {
  int cid = SLSMainConnectionID();
  uint64_t* space_list = NULL;
  int space_count = 0;

  CFArrayRef display_spaces_ref = SLSCopyManagedDisplaySpaces(cid);
  if (display_spaces_ref) {
    int display_spaces_count = CFArrayGetCount(display_spaces_ref);
    for (int i = 0; i < display_spaces_count; ++i) {
      CFDictionaryRef display_ref
                               = CFArrayGetValueAtIndex(display_spaces_ref, i);
      CFArrayRef spaces_ref = CFDictionaryGetValue(display_ref,
                                                   CFSTR("Spaces"));
      int spaces_count = CFArrayGetCount(spaces_ref);

      space_list = (uint64_t*)realloc(space_list,
                                      sizeof(uint64_t)*(space_count
                                                        + spaces_count));

      for (int j = 0; j < spaces_count; ++j) {
        CFDictionaryRef space_ref = CFArrayGetValueAtIndex(spaces_ref, j);
        CFNumberRef sid_ref = CFDictionaryGetValue(space_ref, CFSTR("id64"));
        CFNumberGetValue(sid_ref,
                         CFNumberGetType(sid_ref),
                         space_list + space_count + j);
      }
      space_count += spaces_count;
    }
    CFRelease(display_spaces_ref);
  }

  uint64_t set_tags = 1;
  uint64_t clear_tags = 0;

  CFArrayRef space_list_ref = cfarray_of_cfnumbers(space_list,
                                                   sizeof(uint64_t),
                                                   space_count,
                                                   kCFNumberSInt64Type);

  CFArrayRef window_list_ref = SLSCopyWindowsWithOptionsAndTags(cid,
                                                                0,
                                                                space_list_ref,
                                                                0x2,
                                                                &set_tags,
                                                                &clear_tags  );
  if (window_list_ref) {
    int count = CFArrayGetCount(window_list_ref);
    if (count > 0) {
      CFTypeRef query = SLSWindowQueryWindows(cid, window_list_ref, count);
      CFTypeRef iterator = SLSWindowQueryResultCopyWindows(query);

      while (SLSWindowIteratorAdvance(iterator)) {
        ITERATOR_WINDOW_SUITABLE(iterator, {
          uint32_t wid = SLSWindowIteratorGetWindowID(iterator);
          windows_window_create(windows, wid, window_space_id(cid, wid));
        });
      }

      windows_update_notifications(windows);
      CFRelease(query);
      CFRelease(iterator);
    }
    CFRelease(window_list_ref);
  }
  CFRelease(space_list_ref);
  free(space_list);
}
