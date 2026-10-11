#!/usr/bin/env python3
import os, re, sys, argparse

class ConfigOption:
    def __init__(self, name, opt_type):
        self.name = name
        self.type = opt_type
        self.prompt = ""
        self.depends_on = []
        self.default = None
        self.help_text = ""

class CopperConfigLoader:
    def __init__(self, root_dir):
        self.root_dir = os.path.abspath(root_dir)
        self.configs = {}
        self.parsed_files = set()
        self.root_menu = {'kind': 'menu', 'title': '', 'children': []}
        self.menu_stack = [self.root_menu]

    def find_kconfig_files(self):
        out = []
        for dp, dn, fn in os.walk(self.root_dir):
            dn[:] = [d for d in dn if not d.startswith('.') and d != 'build']
            if 'Kconfig' in fn:
                out.append(os.path.join(dp, 'Kconfig'))
        return out

    def parse_kconfig_file(self, filepath):
        if filepath in self.parsed_files:
            return
        self.parsed_files.add(filepath)
        if not os.path.exists(filepath):
            return
        
        with open(filepath, 'r') as f:
            lines = f.readlines()
            
        cur = None
        in_help = False
        for raw in lines:
            line = raw.rstrip()
            if in_help:
                if line and not line[0] in ' \t':
                    in_help = False
                else:
                    if cur:
                        cur.help_text += line.strip() + "\n"
                    continue
            if not line or line.startswith('#'):
                continue

            m = re.match(r'^menu\s+"(.*)"', line)
            if m:
                node = {'kind': 'menu', 'title': m.group(1), 'children': []}
                self.menu_stack[-1]['children'].append(node)
                self.menu_stack.append(node)
                continue
                
            if re.match(r'^endmenu', line):
                if len(self.menu_stack) > 1:
                    self.menu_stack.pop()
                continue

            m = re.match(r'^config\s+([A-Z0-9_]+)', line)
            if m:
                name = m.group(1)
                cur = ConfigOption(name, "unknown")
                self.configs[name] = cur
                self.menu_stack[-1]['children'].append({'kind': 'config', 'name': name})
                continue

            if cur:
                m = re.match(r'^\s+(bool|string|int|tristate)(?:\s+"(.*)")?', line)
                if m:
                    cur.type = m.group(1)
                    if m.group(2):
                        cur.prompt = m.group(2)
                    continue
                    
                m = re.match(r'^\s+depends\s+on\s+(.+)', line)
                if m:
                    cur.depends_on.append(m.group(1))
                    continue
                    
                m = re.match(r'^\s+default\s+(.+)', line)
                if m:
                    cur.default = m.group(1)
                    continue
                    
                if re.match(r'^\s+help', line):
                    in_help = True
                    continue

            m = re.match(r'^source\s+"(.+)"', line)
            if m:
                sp = m.group(1)
                base = os.path.dirname(filepath)
                rp = os.path.normpath(os.path.join(base, sp))
                if not os.path.exists(rp):
                    rp = os.path.join(self.root_dir, sp)
                self.parse_kconfig_file(rp)

    def load_all(self):
        for f in self.find_kconfig_files():
            self.parse_kconfig_file(f)
        return self.configs

    def load_existing_config(self, p):
        if not os.path.exists(p):
            return
        with open(p, 'r') as f:
            for line in f:
                line = line.strip()
                if line.startswith('CONFIG_') and '=' in line:
                    k, v = line.split('=', 1)
                    n = k.replace('CONFIG_', '')
                    if n in self.configs:
                        self.configs[n].default = v

    def persist(self):
        bools = [n for n, o in self.configs.items() if o.type == 'bool' and o.default == 'y']
        vals = {n: o.default for n, o in self.configs.items() if o.type in ('int', 'string') and o.default is not None}
        self.save_config('.config', bools, vals)

    def save_config(self, path, bools, vals):
        d = os.path.dirname(path)
        if d:
            os.makedirs(d, exist_ok=True)
        with open(path, 'w') as f:
            f.write("#\n# Copper Kernel Configuration\n#\n")
            for n, o in self.configs.items():
                if o.type == 'bool':
                    f.write(f"CONFIG_{n}={'y' if n in bools else 'n'}\n")
                elif o.type in ('int', 'string'):
                    if n in vals:
                        f.write(f"CONFIG_{n}={vals[n]}\n")
                    elif o.default is not None:
                        f.write(f"CONFIG_{n}={o.default}\n")
                    else:
                        f.write(f"# CONFIG_{n} is not set\n")

    def generate_c_header(self, path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w') as f:
            f.write("/* Auto-generated. DO NOT EDIT. */\n#ifndef __AUTOCONF_H\n#define __AUTOCONF_H\n\n")
            for n, o in self.configs.items():
                if o.type == 'bool':
                    f.write(f"#define CONFIG_{n} 1\n" if o.default == 'y' else f"/* CONFIG_{n} is not set */\n")
                elif o.type == 'int':
                    f.write(f"#define CONFIG_{n} {o.default}\n")
                elif o.type == 'string':
                    f.write(f'#define CONFIG_{n} "{o.default}"\n')
            f.write("\n#endif\n")

    def find_menu(self, path):
        node = self.root_menu
        if path:
            for part in path.split('/'):
                nxt = None
                for c in node['children']:
                    if c.get('kind') == 'menu' and c['title'] == part:
                        nxt = c
                        break
                if nxt is None:
                    return None
                node = nxt
        return node

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=".")
    ap.add_argument('--ui-list', default=None)
    ap.add_argument('--ui-toggle', default=None)
    ap.add_argument('--ui-set', nargs=2, default=None)
    ap.add_argument('--generate-headers', action='store_true')
    ap.add_argument('--persist', action='store_true')
    a = ap.parse_args()

    L = CopperConfigLoader(a.root)
    L.load_all()
    L.load_existing_config(".config")

    if a.ui_list is not None:
        node = L.find_menu(a.ui_list)
        if node:
            for c in node['children']:
                if c['kind'] == 'menu':
                    print(f"menu|{c['title']}|{c['title']}||")
                else:
                    o = L.configs.get(c['name'])
                    if o and o.prompt:
                        print(f"{o.type}|{o.name}|{o.prompt}|{o.default or ''}|{' '.join(o.help_text.split())}")
    elif a.ui_toggle:
        o = L.configs.get(a.ui_toggle)
        if o and o.type == 'bool':
            o.default = 'n' if o.default == 'y' else 'y'
            L.persist()
    elif a.ui_set:
        o = L.configs.get(a.ui_set[0])
        if o:
            o.default = a.ui_set[1]
            L.persist()
    elif a.persist:
        L.persist()
    elif a.generate_headers:
        L.generate_c_header("include/generated/autoconf.h")
