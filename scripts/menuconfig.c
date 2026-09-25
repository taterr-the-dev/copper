#include <ctype.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ITEMS 512
#define NAME_SZ 64
#define PROMPT_SZ 128
#define VALUE_SZ 128
#define HELP_SZ 512

enum item_type { T_BOOL, T_INT, T_STRING, T_MENU, T_SEPARATOR };

typedef struct {
  char name[NAME_SZ];
  char prompt[PROMPT_SZ];
  char type_str[16];
  enum item_type type;
  char value[VALUE_SZ];
  char def_val[VALUE_SZ];
  char help[HELP_SZ];
  char deps[NAME_SZ];
  int parent;
  int depth;
} MenuItem;

static MenuItem items[MAX_ITEMS];
static int num_items = 0;

static int current_parent = -1;
static int selected = 0;
static int scroll_top = 0;
static int nav_stack[64];
static int nav_depth = 0;

#define C_TITLE 1
#define C_SEL 2
#define C_MENU 3
#define C_BOOL_Y 4
#define C_BOOL_N 5
#define C_HELP 6
#define C_BORDER 7

static void init_colors(void) {
  start_color();
  use_default_colors();
  init_pair(C_TITLE, COLOR_CYAN, -1);
  init_pair(C_SEL, COLOR_BLACK, COLOR_CYAN);
  init_pair(C_MENU, COLOR_YELLOW, -1);
  init_pair(C_BOOL_Y, COLOR_GREEN, -1);
  init_pair(C_BOOL_N, COLOR_WHITE, -1);
  init_pair(C_HELP, COLOR_WHITE, -1);
  init_pair(C_BORDER, COLOR_BLUE, -1);
}

static int add_item(void) {
  if (num_items >= MAX_ITEMS)
    return -1;
  memset(&items[num_items], 0, sizeof(MenuItem));
  items[num_items].parent = -1;
  return num_items++;
}

static int find_item(const char *name) {
  for (int i = 0; i < num_items; i++)
    if (items[i].name[0] && strcmp(items[i].name, name) == 0)
      return i;
  return -1;
}

static void parse_file(const char *filename, int parent_idx, int depth);

static void parse_file(const char *filename, int parent_idx, int depth) {
  FILE *f = fopen(filename, "r");
  if (!f)
    return;

  char line[512];
  int cur = -1;
  int in_help = 0;
  int help_indent = 0;

  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = '\0';

    int indent = 0;
    while (line[indent] == ' ' || line[indent] == '\t')
      indent++;
    char *p = line + indent;

    if (in_help) {
      if (indent > help_indent && cur >= 0) {
        if (strlen(items[cur].help) + strlen(p) + 2 < HELP_SZ) {
          strcat(items[cur].help, p);
          strcat(items[cur].help, "\n");
        }
        continue;
      }
      in_help = 0;
    }

    if (*p == '\0' || *p == '#')
      continue;

    if (strncmp(p, "config ", 7) == 0) {
      cur = add_item();
      if (cur < 0)
        continue;
      sscanf(p + 7, "%63s", items[cur].name);
      items[cur].parent = parent_idx;
      items[cur].depth = depth;
    } else if (strncmp(p, "menu ", 5) == 0) {
      cur = add_item();
      if (cur < 0)
        continue;
      items[cur].type = T_MENU;
      items[cur].parent = parent_idx;
      items[cur].depth = depth;
      snprintf(items[cur].name, NAME_SZ, "__menu_%d", cur);
      char *q1 = strchr(p, '"');
      if (q1) {
        char *q2 = strchr(q1 + 1, '"');
        if (q2) {
          int len = q2 - q1 - 1;
          if (len > PROMPT_SZ - 1)
            len = PROMPT_SZ - 1;
          strncpy(items[cur].prompt, q1 + 1, len);
          items[cur].prompt[len] = '\0';
        }
      }
      int menu_idx = cur;
      char sub[512];
      int sub_parent = menu_idx;
      int sub_depth = depth + 1;
    } else if (strncmp(p, "endmenu", 7) == 0) {
    } else if (strncmp(p, "source ", 7) == 0) {
      char *q1 = strchr(p, '"');
      if (q1) {
        char *q2 = strchr(q1 + 1, '"');
        if (q2) {
          *q2 = '\0';
          parse_file(q1 + 1, parent_idx, depth);
        }
      }
    } else if (cur >= 0) {
      if (strncmp(p, "bool ", 5) == 0) {
        strcpy(items[cur].type_str, "bool");
        items[cur].type = T_BOOL;
        char *q1 = strchr(p, '"');
        if (q1) {
          char *q2 = strchr(q1 + 1, '"');
          if (q2) {
            int len = q2 - q1 - 1;
            if (len > PROMPT_SZ - 1)
              len = PROMPT_SZ - 1;
            strncpy(items[cur].prompt, q1 + 1, len);
            items[cur].prompt[len] = '\0';
          }
        }
      } else if (strncmp(p, "int ", 4) == 0) {
        strcpy(items[cur].type_str, "int");
        items[cur].type = T_INT;
        char *q1 = strchr(p, '"');
        if (q1) {
          char *q2 = strchr(q1 + 1, '"');
          if (q2) {
            int len = q2 - q1 - 1;
            if (len > PROMPT_SZ - 1)
              len = PROMPT_SZ - 1;
            strncpy(items[cur].prompt, q1 + 1, len);
            items[cur].prompt[len] = '\0';
          }
        }
      } else if (strncmp(p, "string ", 7) == 0) {
        strcpy(items[cur].type_str, "string");
        items[cur].type = T_STRING;
        char *q1 = strchr(p, '"');
        if (q1) {
          char *q2 = strchr(q1 + 1, '"');
          if (q2) {
            int len = q2 - q1 - 1;
            if (len > PROMPT_SZ - 1)
              len = PROMPT_SZ - 1;
            strncpy(items[cur].prompt, q1 + 1, len);
            items[cur].prompt[len] = '\0';
          }
        }
      } else if (strncmp(p, "default ", 8) == 0) {
        char *val = p + 8;
        while (isspace(*val))
          val++;
        if (*val == '"') {
          val++;
          char *q2 = strchr(val, '"');
          if (q2)
            *q2 = '\0';
        }
        strncpy(items[cur].def_val, val, VALUE_SZ - 1);
      } else if (strncmp(p, "help", 4) == 0 &&
                 (isspace(p[4]) || p[4] == '\0')) {
        in_help = 1;
        help_indent = indent;
        items[cur].help[0] = '\0';
      }
    }
  }
  fclose(f);
}

static void parse_kconfig_full(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (!f)
    return;

  char line[512];
  int cur = -1;
  int in_help = 0;
  int help_indent = 0;
  int menu_stack[32];
  int menu_depth = 0;
  menu_stack[0] = -1;

  while (fgets(line, sizeof(line), f)) {
    line[strcspn(line, "\r\n")] = '\0';
    int indent = 0;
    while (line[indent] == ' ' || line[indent] == '\t')
      indent++;
    char *p = line + indent;

    if (in_help) {
      if (indent > help_indent && cur >= 0) {
        if (strlen(items[cur].help) + strlen(p) + 2 < HELP_SZ) {
          strcat(items[cur].help, p);
          strcat(items[cur].help, "\n");
        }
        continue;
      }
      in_help = 0;
    }

    if (*p == '\0' || *p == '#')
      continue;

    if (strncmp(p, "config ", 7) == 0) {
      cur = add_item();
      if (cur < 0)
        continue;
      sscanf(p + 7, "%63s", items[cur].name);
      items[cur].parent = menu_stack[menu_depth];
      items[cur].depth = menu_depth;
    } else if (strncmp(p, "menu ", 5) == 0) {
      cur = add_item();
      if (cur < 0)
        continue;
      items[cur].type = T_MENU;
      items[cur].parent = menu_stack[menu_depth];
      items[cur].depth = menu_depth;
      snprintf(items[cur].name, NAME_SZ, "__menu_%d", cur);
      char *q1 = strchr(p, '"');
      if (q1) {
        char *q2 = strchr(q1 + 1, '"');
        if (q2) {
          int len = q2 - q1 - 1;
          if (len > PROMPT_SZ - 1)
            len = PROMPT_SZ - 1;
          strncpy(items[cur].prompt, q1 + 1, len);
          items[cur].prompt[len] = '\0';
        }
      }
      if (menu_depth < 31) {
        menu_depth++;
        menu_stack[menu_depth] = cur;
      }
      cur = -1;
    } else if (strncmp(p, "endmenu", 7) == 0) {
      if (menu_depth > 0)
        menu_depth--;
      cur = -1;
    } else if (strncmp(p, "source ", 7) == 0) {
      char *q1 = strchr(p, '"');
      if (q1) {
        char *q2 = strchr(q1 + 1, '"');
        if (q2) {
          *q2 = '\0';
          parse_kconfig_full(q1 + 1);
        }
      }
    } else if (cur >= 0) {
      if (strncmp(p, "bool ", 5) == 0) {
        strcpy(items[cur].type_str, "bool");
        items[cur].type = T_BOOL;
        char *q1 = strchr(p, '"');
        if (q1) {
          char *q2 = strchr(q1 + 1, '"');
          if (q2) {
            int len = q2 - q1 - 1;
            if (len > PROMPT_SZ - 1)
              len = PROMPT_SZ - 1;
            strncpy(items[cur].prompt, q1 + 1, len);
            items[cur].prompt[len] = '\0';
          }
        }
      } else if (strncmp(p, "int ", 4) == 0) {
        strcpy(items[cur].type_str, "int");
        items[cur].type = T_INT;
        char *q1 = strchr(p, '"');
        if (q1) {
          char *q2 = strchr(q1 + 1, '"');
          if (q2) {
            int len = q2 - q1 - 1;
            if (len > PROMPT_SZ - 1)
              len = PROMPT_SZ - 1;
            strncpy(items[cur].prompt, q1 + 1, len);
            items[cur].prompt[len] = '\0';
          }
        }
      } else if (strncmp(p, "string ", 7) == 0) {
        strcpy(items[cur].type_str, "string");
        items[cur].type = T_STRING;
        char *q1 = strchr(p, '"');
        if (q1) {
          char *q2 = strchr(q1 + 1, '"');
          if (q2) {
            int len = q2 - q1 - 1;
            if (len > PROMPT_SZ - 1)
              len = PROMPT_SZ - 1;
            strncpy(items[cur].prompt, q1 + 1, len);
            items[cur].prompt[len] = '\0';
          }
        }
      } else if (strncmp(p, "default ", 8) == 0) {
        char *val = p + 8;
        while (isspace(*val))
          val++;
        if (*val == '"') {
          val++;
          char *q2 = strchr(val, '"');
          if (q2)
            *q2 = '\0';
        }
        strncpy(items[cur].def_val, val, VALUE_SZ - 1);
      } else if (strncmp(p, "help", 4) == 0 &&
                 (isspace(p[4]) || p[4] == '\0')) {
        in_help = 1;
        help_indent = indent;
        items[cur].help[0] = '\0';
      } else if (strncmp(p, "depends on ", 11) == 0) {
        char dep[NAME_SZ];
        sscanf(p + 11, "%63s", dep);
        strncpy(items[cur].deps, dep, NAME_SZ - 1);
      }
    }
  }
  fclose(f);
}

static void load_config(void) {
  FILE *f = fopen(".config", "r");
  if (!f)
    return;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    char name[NAME_SZ], val[VALUE_SZ];
    if (sscanf(line, "%63[^=]=%127s", name, val) == 2) {
      int idx = find_item(name);
      if (idx >= 0)
        strncpy(items[idx].value, val, VALUE_SZ - 1);
    }
  }
  fclose(f);
}

static void save_config(void) {
  for (int i = 0; i < num_items; i++) {
    if (items[i].deps[0] && items[i].value[0] == 'y') {
      int dep_idx = find_item(items[i].deps);
      if (dep_idx >= 0) {
        strcpy(items[dep_idx].value, "y");
      }
    }
  }

  FILE *f = fopen(".config", "w");
  if (!f)
    return;
  fprintf(f, "#\n# Automatically generated by Copper menuconfig\n#\n");
  for (int i = 0; i < num_items; i++) {
    if (items[i].name[0] == '\0' || items[i].type == T_MENU)
      continue;
    fprintf(f, "CONFIG_%s=%s\n", items[i].name, items[i].value);
  }
  fclose(f);
}

static void apply_defaults(void) {
  for (int i = 0; i < num_items; i++) {
    if (items[i].value[0] == '\0') {
      if (items[i].def_val[0])
        strncpy(items[i].value, items[i].def_val, VALUE_SZ - 1);
      else if (items[i].type == T_BOOL)
        strcpy(items[i].value, "n");
    }
  }
}

static int count_children(int parent) {
  int count = 0;
  for (int i = 0; i < num_items; i++) {
    if (items[i].parent == parent && items[i].name[0] != '\0')
      count++;
  }
  return count;
}

static int get_child_at(int parent, int index) {
  int count = 0;
  for (int i = 0; i < num_items; i++) {
    if (items[i].parent == parent && items[i].name[0] != '\0') {
      if (count == index)
        return i;
      count++;
    }
  }
  return -1;
}

static void draw_screen(void) {
  int max_y, max_x;
  getmaxyx(stdscr, max_y, max_x);

  clear();

  attron(COLOR_PAIR(C_TITLE) | A_BOLD);
  mvprintw(0, 0, "Copper Kernel Configuration");
  attroff(COLOR_PAIR(C_TITLE) | A_BOLD);

  attron(A_DIM);
  mvprintw(1, 0, "Path: /");
  for (int i = 0; i < nav_depth; i++) {
    printw("%s/", items[nav_stack[i]].prompt);
  }
  attroff(A_DIM);

  int menu_top = 3;
  int menu_bottom = max_y - 3;
  int visible_rows = menu_bottom - menu_top + 1;
  int total_children = count_children(current_parent);

  if (selected < scroll_top)
    scroll_top = selected;
  if (selected >= scroll_top + visible_rows)
    scroll_top = selected - visible_rows + 1;
  if (scroll_top > total_children - visible_rows)
    scroll_top = total_children - visible_rows;
  if (scroll_top < 0)
    scroll_top = 0;

  for (int row = 0; row < visible_rows; row++) {
    int idx = scroll_top + row;
    if (idx >= total_children)
      break;

    int item_idx = get_child_at(current_parent, idx);
    if (item_idx < 0)
      continue;

    int y = menu_top + row;
    int is_selected = (idx == selected);

    int dep_ok = 1;
    if (items[item_idx].deps[0]) {
      int dep_idx = find_item(items[item_idx].deps);
      if (dep_idx >= 0 && items[dep_idx].value[0] != 'y') {
        dep_ok = 0;
      }
    }

    if (is_selected)
      attron(COLOR_PAIR(C_SEL));

    move(y, 2);

    if (items[item_idx].type == T_MENU) {
      attron(COLOR_PAIR(is_selected ? C_SEL : C_MENU));
      printw("  -->  %s", items[item_idx].prompt);
      attroff(COLOR_PAIR(is_selected ? C_SEL : C_MENU));
    } else if (items[item_idx].type == T_BOOL) {
      int enabled = (items[item_idx].value[0] == 'y');
      attron(COLOR_PAIR(is_selected ? C_SEL : (enabled ? C_BOOL_Y : C_BOOL_N)));
      printw("  [%c]  %s", enabled ? '*' : ' ', items[item_idx].prompt);
      attroff(
          COLOR_PAIR(is_selected ? C_SEL : (enabled ? C_BOOL_Y : C_BOOL_N)));
    } else {
      if (is_selected)
        attron(COLOR_PAIR(C_SEL));
      printw("  (%s)  %s", items[item_idx].value, items[item_idx].prompt);
      if (is_selected)
        attroff(COLOR_PAIR(C_SEL));
    }

    if (is_selected)
      attroff(COLOR_PAIR(C_SEL));
  }

  if (scroll_top > 0)
    mvprintw(menu_top - 1, max_x / 2, "--- more ---");
  if (scroll_top + visible_rows < total_children)
    mvprintw(menu_bottom + 1, max_x / 2, "--- more ---");

  attron(COLOR_PAIR(C_BORDER));
  mvhline(max_y - 2, 0, ACS_HLINE, max_x);
  attroff(COLOR_PAIR(C_BORDER));
  mvprintw(max_y - 1, 0,
           " Arrows:Navigate  Enter:Select/Edit  Space:Toggle  Esc:Back  "
           "?:Help  q:Quit");
}

static void edit_value(int item_idx) {
  int max_y, max_x;
  getmaxyx(stdscr, max_y, max_x);

  int win_h = 5;
  int win_w = 60;
  if (win_w > max_x - 4)
    win_w = max_x - 4;
  int win_y = (max_y - win_h) / 2;
  int win_x = (max_x - win_w) / 2;

  WINDOW *win = newwin(win_h, win_w, win_y, win_x);
  box(win, 0, 0);
  mvwprintw(win, 1, 2, "%s", items[item_idx].prompt);
  mvwprintw(win, 3, 2, "> ");
  wrefresh(win);

  echo();
  curs_set(1);
  wmove(win, 3, 4);

  char buf[VALUE_SZ];
  strncpy(buf, items[item_idx].value, VALUE_SZ - 1);
  buf[VALUE_SZ - 1] = '\0';
  wgetnstr(win, buf, VALUE_SZ - 1);
  wgetnstr(win, buf, VALUE_SZ - 1);

  noecho();
  curs_set(0);
  delwin(win);

  if (strlen(buf) > 0)
    strncpy(items[item_idx].value, buf, VALUE_SZ - 1);
}

static void show_help(int item_idx) {
  if (item_idx < 0 || items[item_idx].help[0] == '\0') {
    const char *generic = "No help available for this option.\n\n"
                          "Use arrows to navigate.\n"
                          "Enter to select menus or edit values.\n"
                          "Space to toggle boolean options.\n"
                          "Esc to go back to the previous menu.\n"
                          "q to save and quit.";
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int h = 10, w = 60;
    if (w > max_x - 4)
      w = max_x - 4;
    if (h > max_y - 4)
      h = max_y - 4;
    WINDOW *win = newwin(h, w, (max_y - h) / 2, (max_x - w) / 2);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "Help");
    mvwprintw(win, 3, 2, "%s", generic);
    mvwprintw(win, h - 2, 2, "Press any key to close");
    wrefresh(win);
    getch();
    delwin(win);
    return;
  }

  int max_y, max_x;
  getmaxyx(stdscr, max_y, max_x);
  int h = 12, w = 70;
  if (w > max_x - 4)
    w = max_x - 4;
  if (h > max_y - 4)
    h = max_y - 4;
  WINDOW *win = newwin(h, w, (max_y - h) / 2, (max_x - w) / 2);
  box(win, 0, 0);
  mvwprintw(win, 1, 2, "Help: %s", items[item_idx].prompt);

  char *help = items[item_idx].help;
  int row = 3;
  char *tok = strtok(help, "\n");
  while (tok && row < h - 2) {
    mvwprintw(win, row, 2, "%.*s", w - 4, tok);
    row++;
    tok = strtok(NULL, "\n");
  }

  mvwprintw(win, h - 2, 2, "Press any key to close");
  wrefresh(win);
  getch();
  delwin(win);
}

int main(void) {
  parse_kconfig_full("Kconfig");
  load_config();
  apply_defaults();

  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  curs_set(0);

  if (has_colors())
    init_colors();

  int running = 1;
  while (running) {
    draw_screen();
    int ch = getch();

    int total = count_children(current_parent);
    if (total == 0) {
      if (nav_depth > 0) {
        nav_depth--;
        current_parent = (nav_depth == 0) ? -1 : nav_stack[nav_depth - 1];
        selected = 0;
        scroll_top = 0;
      } else {
        running = 0;
      }
      continue;
    }

    switch (ch) {
    case KEY_UP:
    case 'k':
      if (selected > 0)
        selected--;
      break;

    case KEY_DOWN:
    case 'j':
      if (selected < total - 1)
        selected++;
      break;

    case '\n':
    case KEY_ENTER:
    case ' ': {
      int item_idx = get_child_at(current_parent, selected);
      if (item_idx < 0)
        break;

      if (ch == ' ' && items[item_idx].type == T_BOOL) {
        int dep_ok = 1;
        if (items[item_idx].deps[0]) {
          int dep_idx = find_item(items[item_idx].deps);
          if (dep_idx >= 0 && items[dep_idx].value[0] != 'y')
            dep_ok = 0;
        }
        if (dep_ok) {
          strcpy(items[item_idx].value,
                 items[item_idx].value[0] == 'y' ? "n" : "y");
        }
      } else if (items[item_idx].type == T_MENU) {
        if (nav_depth < 63) {
          nav_stack[nav_depth] = item_idx;
          nav_depth++;
          current_parent = item_idx;
          selected = 0;
          scroll_top = 0;
        }
      } else if (items[item_idx].type == T_BOOL) {
        strcpy(items[item_idx].value,
               items[item_idx].value[0] == 'y' ? "n" : "y");
      } else {
        edit_value(item_idx);
      }
      break;
    }

    case 27:
    case 'b':
      if (nav_depth > 0) {
        nav_depth--;
        if (nav_depth == 0) {
          current_parent = -1;
        } else {
          current_parent = nav_stack[nav_depth - 1];
        }
        selected = 0;
        scroll_top = 0;
      } else {
        running = 0;
      }
      break;

    case 'q':
    case 'Q':
      running = 0;
      break;

    case '?':
    case 'h': {
      int item_idx = get_child_at(current_parent, selected);
      show_help(item_idx);
      break;
    }

    case KEY_HOME:
      selected = 0;
      scroll_top = 0;
      break;

    case KEY_END:
      selected = total - 1;
      break;

    case KEY_PPAGE: {
      int max_y, max_x;
      getmaxyx(stdscr, max_y, max_x);
      int page = max_y - 6;
      selected -= page;
      if (selected < 0)
        selected = 0;
      break;
    }

    case KEY_NPAGE: {
      int max_y, max_x;
      getmaxyx(stdscr, max_y, max_x);
      int page = max_y - 6;
      selected += page;
      if (selected >= total)
        selected = total - 1;
      break;
    }
    }
  }

  endwin();
  save_config();
  printf("[*] Configuration saved to .config\n");
  return 0;
}
