std = "lua54"
codes = true
max_line_length = false
max_cyclomatic_complexity = 10
unused_args = false

files["config/xr-controls.lua"] = {
    globals = {"omarchy_xr_controls", "omarchy_xr_canvas", "hl"},
}
files["tests/controls.lua"] = {
    globals = {"omarchy_xr_controls", "omarchy_xr_canvas", "hl"},
    ignore = {"122"},
}
