include <NopSCADlib/lib.scad>

box_w = 80;
box_d = 80;
box_h = 31;
wall = 1.2;
box_corner_r = 3;

lid_t = 2;

lid_screw_d = 2.7;
lid_tap_d = 2.2;
lid_boss_od = 7;
lid_boss_r = lid_boss_od / 2;
lid_screw_head_d = 6.4;
lid_screw_head_h = 2.2;

esp32_board_l = 55;
esp32_board_w = 28;
esp32_board_t = 1.6;
esp32_corner_r = 2;

esp32_hole_margin_x = 2.4;
esp32_hole_margin_y = 2.3;
esp32_support_h = 4;
esp32_support_d = 7;
esp32_peg_d = 1.5;
esp32_peg_h = 3;
esp32_float_gap = 2;

front_hole_inset_y = max(0, esp32_hole_margin_y - 1);
rear_hole_inset_y = esp32_hole_margin_y;
rear_tube_extra_gap = 0.5;
screw_tube_h = 4;
screw_tube_hole_d = 2.2;
screw_tube_wall = 1.8;

preview_usb_w = 9;
preview_usb_d = 7;
preview_usb_h = 3.4;
preview_usb_shell_t = 0.6;
preview_usb_gap_below_board = 0;
preview_usb_overlap_with_board = 5.6;
usb_cutout_clearance_w = 0.6;
usb_cutout_clearance_above = 0.2;
usb_cutout_clearance_below = 0.2;
usb_relief_w = 15;
usb_relief_h = 9;
usb_relief_wall_t = 1;
preview_header_len = 46;
preview_header_w = 2.6;
preview_header_h = 18;
preview_header_inset = 1.7;

// Botonera dimensions
btn_base_w = 66;
btn_base_d = 64;
btn_base_t = 4;
btn_top_w  = 60;
btn_top_d  = 57;
btn_top_t  = 4;
btn_corner_r = 2;

show_esp_preview     = true;
show_botonera_preview = true;
part = "assembly"; // assembly, body, lid

esp32_board = pcb(
    "ESP32_38PIN_USBC_SUPPORT",
    "ESP32 38-pin USB-C board support reference",
    [esp32_board_l, esp32_board_w, esp32_board_t],
    corner_r = esp32_corner_r,
    hole_d = 2.2,
    colour = "#2140BE"
);

esp32_center = [58, 30];
esp32_board_bottom_z = wall + esp32_support_h + esp32_float_gap;
esp32_support_top_z = wall + esp32_support_h;
usb_cutout_w = preview_usb_w + usb_cutout_clearance_w;
usb_cutout_h = preview_usb_h + usb_cutout_clearance_above + usb_cutout_clearance_below;
usb_cutout_z = esp32_support_top_z - preview_usb_gap_below_board - preview_usb_h - usb_cutout_clearance_below + 2;
esp32_side_offset = pcb_width(esp32_board) / 2 - esp32_hole_margin_x;
esp32_front_x = -pcb_length(esp32_board) / 2 + front_hole_inset_y;
esp32_rear_x = pcb_length(esp32_board) / 2 - rear_hole_inset_y;
esp32_rear_tube_x = pcb_length(esp32_board) / 2 + screw_tube_hole_d / 2 + rear_tube_extra_gap;

corner_boss_x = [wall + lid_boss_r, box_w - wall - lid_boss_r];
corner_boss_y = [wall + lid_boss_r, box_d - wall - lid_boss_r];

vent_w           = 4;
vent_h           = 5;
vent_rows        = 4;
vent_row_spacing = 7;
vent_z_center    = 14.5;  // rows at z=4,11,18,25 within box_h=31

vent_even_cols = [-24.5, -17.5, -10.5, -3.5, 3.5, 10.5, 17.5, 24.5];
vent_odd_cols  = [-21, -14, -7, 0, 7, 14, 21];

vent_front_even_cols = [-24.5, -17.5, -10.5, -3.5, 3.5];
vent_front_odd_cols  = [-21, -14, -7, 0];

module rounded_rect_2d(w, d, r) {
    rr = min(r, w / 2, d / 2);
    hull()
        for (x = [rr, w - rr], y = [rr, d - rr])
            translate([x, y])
                circle(r = rr, $fn = 32);
}

module lid_screw_cut(lid_h) {
    countersink_h = min(lid_screw_head_h, lid_h + 0.1);
    translate([0, 0, -0.1])
        cylinder(d = lid_screw_d, h = lid_h + 0.2, $fn = 20);
    translate([0, 0, lid_h - countersink_h])
        cylinder(d1 = lid_screw_d, d2 = lid_screw_head_d, h = countersink_h + 0.1, $fn = 32);
}

module place_esp(z = esp32_board_bottom_z) {
    translate([esp32_center.x, esp32_center.y, z])
        rotate([0, 0, 90])
            children();
}

module esp32_support_post(with_peg = true) {
    support_wall = 1.2;
    difference() {
        cylinder(d = esp32_support_d, h = esp32_support_h, $fn = 28);
        translate([0, 0, -0.1])
            cylinder(d = esp32_support_d - 2 * support_wall, h = esp32_support_h - support_wall + 0.1, $fn = 20);
    }
    if (with_peg)
        translate([0, 0, esp32_support_h])
            cylinder(d = esp32_peg_d, h = esp32_peg_h, $fn = 20);
}

module screw_tube(h = screw_tube_h, hole_d = screw_tube_hole_d, wall_t = screw_tube_wall) {
    difference() {
        cylinder(d = hole_d + 2 * wall_t, h = h, $fn = 28);
        translate([0, 0, -0.1])
            cylinder(d = hole_d, h = h + 0.2, $fn = 20);
    }
}

module usb_cutout() {
    translate([esp32_center.x - usb_cutout_w / 2, -0.1, usb_cutout_z])
        cube([usb_cutout_w, wall + 0.2, usb_cutout_h]);
}

module usb_outer_relief() {
    translate([
        esp32_center.x - usb_relief_w / 2,
        -0.1,
        usb_cutout_z - (usb_relief_h - usb_cutout_h) / 2
    ])
        cube([usb_relief_w, wall - usb_relief_wall_t + 0.1, usb_relief_h]);
}

module esp32_supports() {
    place_esp(wall) {
        for (side_y = [-esp32_side_offset, esp32_side_offset]) {
            translate([esp32_front_x, side_y, 0])
                esp32_support_post(with_peg = false);
            translate([esp32_rear_x, side_y, 0])
                esp32_support_post(with_peg = true);
        }
        translate([esp32_rear_tube_x, 0, 0])
            screw_tube();
    }
}

module esp32_preview() {
    place_esp()
        color([0, 0, 1, 0.35])
            pcb(esp32_board);

    place_esp() {
        color([0.82, 0.82, 0.85, 0.55])
            translate([
                -esp32_board_l / 2 + preview_usb_overlap_with_board - preview_usb_d / 2,
                0,
                -preview_usb_gap_below_board - preview_usb_h / 2
            ])
                cube([preview_usb_d, preview_usb_w, preview_usb_h], center = true);

        color([0.15, 0.15, 0.15, 0.45])
            translate([
                -esp32_board_l / 2 + preview_usb_overlap_with_board - preview_usb_d / 2 + 0.4,
                0,
                -preview_usb_gap_below_board - preview_usb_h / 2
            ])
                cube([
                    preview_usb_d - 2 * preview_usb_shell_t,
                    preview_usb_w - 2 * preview_usb_shell_t,
                    preview_usb_h - 2 * preview_usb_shell_t
                ], center = true);

        color([0.05, 0.05, 0.05, 0.45])
            for (sy = [-1, 1])
                translate([
                    0,
                    sy * (esp32_board_w / 2 - preview_header_inset),
                    esp32_board_t + preview_header_h / 2
                ])
                    cube([preview_header_len, preview_header_w, preview_header_h], center = true);
    }
}

module rounded_btn_2d(w, d, r) {
    rr = min(r, w / 2, d / 2);
    hull()
        for (x = [rr, w - rr], y = [rr, d - rr])
            translate([x, y])
                circle(r = rr, $fn = 32);
}

btn_off_x = (box_w - btn_base_w) / 2;          // = 7  (centered)
btn_off_y = (box_d - btn_base_d) / 2;          // = 8  (centered)

// Centered botonera preview
module botonera_preview() {
    base_z = box_h - btn_base_t;
    color([0.2, 0.8, 0.3, 0.45]) {
        translate([btn_off_x, btn_off_y, base_z])
            linear_extrude(height = btn_base_t)
                rounded_btn_2d(btn_base_w, btn_base_d, btn_corner_r);
        translate([
            btn_off_x + (btn_base_w - btn_top_w) / 2,
            btn_off_y + (btn_base_d - btn_top_d) / 2,
            box_h
        ])
            linear_extrude(height = btn_top_t)
                rounded_btn_2d(btn_top_w, btn_top_d, btn_corner_r);
    }
}

col_sz = 6;

// Front-left corner: full L-bracket.
// Front-right corner: only extends toward right wall (no inner-x arm)
//   to avoid conflicting with ESP32 space at x=44..72.
module btn_front_columns() {
    // front-left: full square centered on corner (7, btn_off_y)
    translate([btn_off_x - col_sz, btn_off_y - col_sz, wall])
        cube([col_sz * 2, col_sz * 2, box_h - wall]);
    // front-right: only rightward from x=73 — "pegada a la pared"
    translate([btn_off_x + btn_base_w, btn_off_y - col_sz, wall])
        cube([col_sz, col_sz * 2, box_h - wall]);
}

// Back two corners: full L-bracket columns.
module btn_back_columns() {
    back_y = btn_off_y + btn_base_d;
    for (cx = [btn_off_x, btn_off_x + btn_base_w])
        translate([cx - col_sz, back_y - col_sz, wall])
            cube([col_sz * 2, col_sz * 2, box_h - wall]);
}

// Carves botonera footprint at z=18..22 from all columns.
module btn_botonera_cut() {
    translate([btn_off_x, btn_off_y, box_h - btn_base_t])
        linear_extrude(height = btn_base_t + 0.1)
            rounded_btn_2d(btn_base_w, btn_base_d, btn_corner_r);
}

// Clears screw-head access above each corner boss where columns overlap.
module column_screw_clearance() {
    for (bx = corner_boss_x)
        for (by = corner_boss_y)
            translate([bx, by, box_h - lid_screw_head_h - 0.1])
                cylinder(d = lid_screw_head_d + 1, h = lid_screw_head_h + 0.2, $fn = 28);
}

module hanging_corner_boss(cx, cy, sx, sy) {
    hang = 10;
    gusset_h = 10;
    start_z = box_h - hang;
    span = lid_boss_od + wall;

    x_arm_x = sx < 0 ? -lid_boss_r - wall : -lid_boss_r;
    y_arm_y = sy < 0 ? -lid_boss_r - wall : -lid_boss_r;
    x_wall = sx < 0 ? -lid_boss_r - wall : lid_boss_r + wall - 0.01;
    y_wall = sy < 0 ? -lid_boss_r - wall : lid_boss_r + wall - 0.01;

    difference() {
        intersection() {
            union() {
                translate([0, 0, start_z])
                    cylinder(d = lid_boss_od, h = hang, $fn = 28);
                translate([x_arm_x, -lid_boss_r, start_z])
                    cube([span, lid_boss_od, hang]);
                translate([-lid_boss_r, y_arm_y, start_z])
                    cube([lid_boss_od, span, hang]);
                hull() {
                    translate([x_arm_x, -lid_boss_r, start_z - 0.01])
                        cube([span, lid_boss_od, 0.01]);
                    translate([x_wall, -lid_boss_r, start_z - gusset_h])
                        cube([0.01, lid_boss_od, 0.01]);
                }
                hull() {
                    translate([-lid_boss_r, y_arm_y, start_z - 0.01])
                        cube([lid_boss_od, span, 0.01]);
                    translate([-lid_boss_r, y_wall, start_z - gusset_h])
                        cube([lid_boss_od, 0.01, 0.01]);
                }
            }
            translate([-cx, -cy, 0])
                linear_extrude(height = box_h)
                    rounded_rect_2d(box_w, box_d, box_corner_r);
        }
        translate([0, 0, box_h - 8])
            cylinder(d = lid_tap_d, h = 8.1, $fn = 16);
    }
}

lid_vent_rows = 9;

module lid_staggered_grid_2d() {
    for (row = [0 : lid_vent_rows - 1]) {
        y_local = row * vent_row_spacing - (lid_vent_rows - 1) * vent_row_spacing / 2;
        cols = (row % 2 == 0) ? vent_even_cols : vent_odd_cols;
        for (cx = cols)
            translate([cx, y_local])
                diamond_2d();
    }
}

module lid_vents() {
    translate([box_w / 2, box_d / 2, -0.1])
        linear_extrude(height = lid_t + 0.2)
            lid_staggered_grid_2d();
}

module diamond_2d() {
    polygon(points = [
        [0,  vent_h / 2],
        [ vent_w / 2, 0],
        [0, -vent_h / 2],
        [-vent_w / 2, 0]
    ]);
}

module staggered_grid_2d(even_cols, odd_cols) {
    for (row = [0 : vent_rows - 1]) {
        z_local = row * vent_row_spacing - (vent_rows - 1) * vent_row_spacing / 2;
        cols = (row % 2 == 0) ? even_cols : odd_cols;
        for (cx = cols)
            translate([cx, z_local])
                diamond_2d();
    }
}

module wall_vent_cuts() {
    cut_d = wall + 0.4;

    translate([box_w / 2, -0.1, vent_z_center])
        rotate([-90, 0, 0])
            linear_extrude(height = cut_d)
                staggered_grid_2d(vent_front_even_cols, vent_front_odd_cols);

    translate([box_w / 2, box_d - wall - 0.1, vent_z_center])
        rotate([-90, 0, 0])
            linear_extrude(height = cut_d)
                staggered_grid_2d(vent_even_cols, vent_odd_cols);

    translate([-0.1, box_d / 2, vent_z_center])
        rotate([0, 0, 90])
        rotate([90, 0, 0])
            linear_extrude(height = cut_d)
                staggered_grid_2d(vent_even_cols, vent_odd_cols);

    translate([box_w - wall - 0.1, box_d / 2, vent_z_center])
        rotate([0, 0, 90])
        rotate([90, 0, 0])
            linear_extrude(height = cut_d)
                staggered_grid_2d(vent_even_cols, vent_odd_cols);
}

module box_body() {
    // Box shell (no front pads/slots — replaced by columns below)
    difference() {
        linear_extrude(height = box_h)
            rounded_rect_2d(box_w, box_d, box_corner_r);
        translate([wall, wall, wall])
            linear_extrude(height = box_h)
                rounded_rect_2d(
                    box_w - 2 * wall,
                    box_d - 2 * wall,
                    max(box_corner_r - wall, 0.01)
                );
        usb_cutout();
        usb_outer_relief();
        wall_vent_cuts();
    }

    // Interior features added after inner-hollow (not erased by it)
    esp32_supports();

    for (x = corner_boss_x)
        for (y = corner_boss_y)
            translate([x, y, 0])
                hanging_corner_boss(
                    x, y,
                    x < box_w / 2 ? -1 : 1,
                    y < box_d / 2 ? -1 : 1
                );

    // All four corner columns with botonera pocket and screw clearance carved out
    difference() {
        union() {
            btn_front_columns();
            btn_back_columns();
        }
        btn_botonera_cut();
        column_screw_clearance();
    }

    if (show_esp_preview)
        %esp32_preview();

    if (show_botonera_preview)
        %botonera_preview();
}

module lid_shape() {
    difference() {
        linear_extrude(height = lid_t)
            rounded_rect_2d(box_w, box_d, box_corner_r);
        for (x = corner_boss_x)
            for (y = corner_boss_y)
                translate([x, y, 0])
                    lid_screw_cut(lid_t);
        // hole for botonera top step
        translate([
            btn_off_x + (btn_base_w - btn_top_w) / 2,
            btn_off_y + (btn_base_d - btn_top_d) / 2,
            -0.1
        ])
            linear_extrude(height = lid_t + 0.2)
                rounded_btn_2d(btn_top_w, btn_top_d, btn_corner_r);
    }
}

if (part == "body")
    box_body();
else if (part == "lid")
    lid_shape();
else {
    box_body();
    translate([box_w + 20, 0, 0])
        lid_shape();
}
