#!/ bin / bash
declare - a stack = () current = "" joinpath() {
  if
    [-z "$1"];
  then echo "$2";
  else printf '%s\x1f%s' "$1"
                         "$2";
  fi;
}

while
  true;
do
  entries = $(python3 scripts / copper_config.py-- root.--ui - list "$current")

      args = ();
tags = ();
kinds = () while IFS = '|' read - r kind tag label value help;
do
  [-z "$kind"] &&continue tags += ("$tag"); kinds+=("$kind")
        case "$kind" in
            menu)       disp="--> $label" ;;
            bool)       if [ "$value" = "y" ]; then disp="[*] $label"; else disp="[ ] $label"; fi ;;
            int|string) disp="($value) $label" ;;
        esac
        args+=("$tag" "$disp" "$help")
    done <<< "$entries"

    if [ ${#args[@]} -eq 0 ]; then
        dialog --msgbox "(empty menu)" 8 30
        if [ ${#stack[@]} -eq 0 ]; then break; fi
        current="${stack[-1]}"; unset 'stack[-1]'; continue
    fi

    choice=$(dialog --clear --colors \
        --title "Copper Kernel Configuration" \
        --backtitle "Copper v0.1  |  /${current//$'\x1f'//}" \
        --ok-label "Select" --cancel-label "Exit" \
        --item-help \
        --menu "Arrows=move  Enter=select  Esc=back" 0 0 0 "${args[@]}" \
        3>&1 1>&2 2>&3)
    rc=$?

    if [ $rc -ne 0 ]; then
        if [ ${#stack[@]} -eq 0 ]; then break; fi
        current="${stack[-1]}"; unset 'stack[-1]'; continue
    fi

    kind=""; value=""
    for i in "${!tags[@]}"; do
        if [ "${tags[$i]}" = "$choice" ]; then kind="${kinds[$i]}"; break; fi
    done

    case "$kind" in
        menu)
            stack+=("$current")
            current=$(joinpath "$current" "$choice")
            ;;
        bool)
            python3 scripts/copper_config.py --root . --ui-toggle "$choice"
            ;;
        int|string)
            newval=$(dialog --clear --title "Edit $choice" --inputbox "Value:" 10 50 "$value" 3>&1 1>&2 2>&3)
            [ $? -eq 0 ] && python3 scripts/copper_config.py --root . --ui-set "$choice" "$newval"
            ;;
    esac
done

python3 scripts/copper_config.py --root . --persist
python3 scripts/copper_config.py --root . --generate-headers
echo "[*] Configuration saved to .config and include/generated/autoconf.h"
