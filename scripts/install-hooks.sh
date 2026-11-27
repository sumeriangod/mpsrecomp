#!/bin/sh
# Install a pre-commit hook that refuses commits containing proprietary game files.
set -e
root=$(git rev-parse --show-toplevel)
hook="$root/.git/hooks/pre-commit"
cat > "$hook" <<'EOF'
#!/bin/sh
# (python3 first; on Windows that name can be the Microsoft Store's placeholder, which runs nothing: then python)
for py in python3 python; do
    if "$py" -c "" 2>/dev/null; then exec "$py" "$(git rev-parse --show-toplevel)/scripts/check_no_proprietary.py" --staged; fi
done
echo "pre-commit: no Python found to run scripts/check_no_proprietary.py" >&2
exit 1
EOF
chmod +x "$hook"
echo "installed $hook"
