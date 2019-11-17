// 3D World - Building Generation
// by Frank Gennari
// 5/22/17

#include "3DWorld.h"
#include "function_registry.h"
#include "shaders.h"
#include "file_utils.h"
#include "buildings.h"
#include "mesh.h"
#include "draw_utils.h" // for point_sprite_drawer_sized

using std::string;

bool const DRAW_WINDOWS_AS_HOLES = 1; // somewhat works, but doesn't draw buildings and terrain behind the windows due to incorrect draw order
float const WIND_LIGHT_ON_RAND   = 0.08;

extern bool start_in_inf_terrain, draw_building_interiors;
extern int rand_gen_index, display_mode;
extern point sun_pos;

building_params_t global_building_params;

void get_all_model_bcubes(vector<cube_t> &bcubes); // from model3d.h


void tid_nm_pair_t::set_gl() const {
	select_texture(tid);
	select_multitex(((nm_tid < 0) ? FLAT_NMAP_TEX : nm_tid), 5);
}
void tid_nm_pair_t::toggle_transparent_windows_mode() { // hack
	if      (tid == BLDG_WINDOW_TEX) {tid = BLDG_WIND_TRANS_TEX;}
	else if (tid == BLDG_WIND_TRANS_TEX) {tid = BLDG_WINDOW_TEX;}
}

void color_range_t::gen_color(colorRGBA &color, rand_gen_t &rgen) const {
	if (cmin == cmax) {color = cmin;} // single exact color
	else {UNROLL_4X(color[i_] = rgen.rand_uniform(cmin[i_], cmax[i_]);)}
	if (grayscale_rand > 0.0) {
		float const v(grayscale_rand*rgen.rand_float());
		UNROLL_3X(color[i_] += v;)
	}
}

void building_mat_t::update_range(vector3d const &range_translate) {
	if (place_radius > 0.0) { // clip range to place_radius
		point const center(pos_range.get_cube_center());
			
		for (unsigned d = 0; d < 2; ++d) { // x,y
			max_eq(pos_range.d[d][0], (center[d] - place_radius));
			min_eq(pos_range.d[d][1], (center[d] + place_radius));
		}
	}
	pos_range += range_translate;
}

void building_params_t::add_cur_mat() {
	unsigned const mat_ix(materials.size());
		
	for (unsigned n = 0; n < cur_prob; ++n) { // add more references to this mat for higher probability
		mat_gen_ix.push_back(mat_ix);
		(cur_mat.no_city ? mat_gen_ix_nocity : mat_gen_ix_city).push_back(mat_ix);
	}
	materials.push_back(cur_mat);
	materials.back().update_range(range_translate);
	has_normal_map |= cur_mat.has_normal_map();
}
unsigned building_params_t::choose_rand_mat(rand_gen_t &rgen, bool city_only, bool non_city_only) const {
	vector<unsigned> const &mat_ix_list(get_mat_list(city_only, non_city_only));
	assert(!mat_ix_list.empty());
	return mat_ix_list[rgen.rand()%mat_ix_list.size()];
}
void building_params_t::set_pos_range(cube_t const &pos_range) {
	//cout << "pos_range: " << pos_range.str() << endl;
	cur_mat.set_pos_range(pos_range);
	for (auto i = materials.begin(); i != materials.end(); ++i) {i->set_pos_range(pos_range);}
}
void building_params_t::restore_prev_pos_range() {
	cur_mat.restore_prev_pos_range();
	for (auto i = materials.begin(); i != materials.end(); ++i) {i->restore_prev_pos_range();}
}


float building_mat_t::get_window_tx() const {return wind_xscale*global_building_params.get_window_tx();}
float building_mat_t::get_window_ty() const {return wind_yscale*global_building_params.get_window_ty();}

void buildings_file_err(string const &str, int &error) {
	cout << "Error reading buildings config option " << str << "." << endl;
	error = 1;
}
bool check_01(float v) {return (v >= 0.0 && v <= 1.0);}

int read_building_texture(FILE *fp, string const &str, int &error) {
	char strc[MAX_CHARS] = {0};
	if (!read_str(fp, strc)) {buildings_file_err(str, error);}
	int const ret(get_texture_by_name(std::string(strc), 0, global_building_params.tex_inv_y, global_building_params.get_wrap_mir()));
	//cout << "texture filename: " << str << ", ID: " << ret << endl;
	return ret;
}
void read_building_tscale(FILE *fp, tid_nm_pair_t &tex, string const &str, int &error) {
	if (!read_float(fp, tex.tscale_x)) {buildings_file_err(str, error);}
	tex.tscale_y = tex.tscale_x; // uniform
}

bool parse_buildings_option(FILE *fp) {

	int error(0);
	char strc[MAX_CHARS] = {0};
	if (!read_str(fp, strc)) return 0;
	string const str(strc);

	// global parameters
	if (str == "flatten_mesh") {
		if (!read_bool(fp, global_building_params.flatten_mesh)) {buildings_file_err(str, error);}
	}
	else if (str == "num_place") {
		if (!read_uint(fp, global_building_params.num_place)) {buildings_file_err(str, error);}
	}
	else if (str == "num_tries") {
		if (!read_uint(fp, global_building_params.num_tries)) {buildings_file_err(str, error);}
	}
	else if (str == "ao_factor") {
		if (!read_zero_one_float(fp, global_building_params.ao_factor)) {buildings_file_err(str, error);}
	}
	else if (str == "sec_extra_spacing") {
		if (!read_float(fp, global_building_params.sec_extra_spacing)) {buildings_file_err(str, error);}
	}
	else if (str == "tt_only") {
		if (!read_bool(fp, global_building_params.tt_only)) {buildings_file_err(str, error);}
	}
	else if (str == "infinite_buildings") {
		if (!read_bool(fp, global_building_params.infinite_buildings)) {buildings_file_err(str, error);}
	}
	// material parameters
	else if (str == "range_translate") { // x,y only
		if (!(read_float(fp, global_building_params.range_translate.x) &&
			read_float(fp, global_building_params.range_translate.y))) {buildings_file_err(str, error);}
	}
	else if (str == "pos_range") {
		if (!read_cube(fp, global_building_params.cur_mat.pos_range, 1)) {buildings_file_err(str, error);}
	}
	else if (str == "place_radius") {
		if (!read_float(fp, global_building_params.cur_mat.place_radius)) {buildings_file_err(str, error);}
	}
	else if (str == "max_delta_z") {
		if (!read_float(fp, global_building_params.cur_mat.max_delta_z)) {buildings_file_err(str, error);}
	}
	else if (str == "min_level_height") {
		if (!read_float(fp, global_building_params.cur_mat.min_level_height)) {buildings_file_err(str, error);}
	}
	else if (str == "max_rot_angle") {
		if (!read_float(fp, global_building_params.cur_mat.max_rot_angle)) {buildings_file_err(str, error);}
		global_building_params.cur_mat.max_rot_angle *= TO_RADIANS; // specified in degrees, stored in radians
	}
	else if (str == "split_prob") {
		if (!read_zero_one_float(fp, global_building_params.cur_mat.split_prob)) {buildings_file_err(str, error);}
	}
	else if (str == "cube_prob") {
		if (!read_zero_one_float(fp, global_building_params.cur_mat.cube_prob)) {buildings_file_err(str, error);}
	}
	else if (str == "round_prob") {
		if (!read_zero_one_float(fp, global_building_params.cur_mat.round_prob)) {buildings_file_err(str, error);}
	}
	else if (str == "alt_step_factor_prob") {
		if (!read_zero_one_float(fp, global_building_params.cur_mat.asf_prob)) {buildings_file_err(str, error);}
	}
	else if (str == "min_levels") {
		if (!read_uint(fp, global_building_params.cur_mat.min_levels)) {buildings_file_err(str, error);}
	}
	else if (str == "max_levels") {
		if (!read_uint(fp, global_building_params.cur_mat.max_levels)) {buildings_file_err(str, error);}
	}
	else if (str == "min_sides") {
		if (!read_uint(fp, global_building_params.cur_mat.min_sides)) {buildings_file_err(str, error);}
		if (global_building_params.cur_mat.min_sides < 3) {buildings_file_err(str+" (< 3)", error);}
	}
	else if (str == "max_sides") {
		if (!read_uint(fp, global_building_params.cur_mat.max_sides)) {buildings_file_err(str, error);}
		if (global_building_params.cur_mat.max_sides < 3) {buildings_file_err(str+" (< 3)", error);}
	}
	else if (str == "min_flat_side_amt") {
		if (!read_float(fp, global_building_params.cur_mat.min_fsa)) {buildings_file_err(str, error);}
	}
	else if (str == "max_flat_side_amt") {
		if (!read_float(fp, global_building_params.cur_mat.max_fsa)) {buildings_file_err(str, error);}
	}
	else if (str == "min_alt_step_factor") {
		if (!read_float(fp, global_building_params.cur_mat.min_asf)) {buildings_file_err(str, error);}
	}
	else if (str == "max_alt_step_factor") {
		if (!read_float(fp, global_building_params.cur_mat.max_asf)) {buildings_file_err(str, error);}
	}
	else if (str == "size_range") {
		if (!read_cube(fp, global_building_params.cur_mat.sz_range)) {buildings_file_err(str, error);}
	}
	else if (str == "min_altitude") {
		if (!read_float(fp, global_building_params.cur_mat.min_alt)) {buildings_file_err(str, error);}
	}
	else if (str == "max_altitude") {
		if (!read_float(fp, global_building_params.cur_mat.max_alt)) {buildings_file_err(str, error);}
	}
	else if (str == "no_city") {
		if (!read_bool(fp, global_building_params.cur_mat.no_city)) {buildings_file_err(str, error);}
	}
	// material textures
	else if (str == "texture_mirror") {
		if (!read_bool(fp, global_building_params.tex_mirror)) {buildings_file_err(str, error);}
	}
	else if (str == "texture_inv_y") {
		if (!read_bool(fp, global_building_params.tex_inv_y)) {buildings_file_err(str, error);}
	}
	else if (str == "side_tscale") {read_building_tscale(fp, global_building_params.cur_mat.side_tex, str, error);} // both X and Y
	else if (str == "side_tscale_x") {
		if (!read_float(fp, global_building_params.cur_mat.side_tex.tscale_x)) {buildings_file_err(str, error);}
	}
	else if (str == "side_tscale_y") {
		if (!read_float(fp, global_building_params.cur_mat.side_tex.tscale_y)) {buildings_file_err(str, error);}
	}
	else if (str == "roof_tscale")  {read_building_tscale(fp, global_building_params.cur_mat.roof_tex,  str, error);} // both X and Y
	else if (str == "wall_tscale")  {read_building_tscale(fp, global_building_params.cur_mat.wall_tex,  str, error);} // both X and Y
	else if (str == "ceil_tscale")  {read_building_tscale(fp, global_building_params.cur_mat.ceil_tex,  str, error);} // both X and Y
	else if (str == "floor_tscale") {read_building_tscale(fp, global_building_params.cur_mat.floor_tex, str, error);} // both X and Y
	// building textures
	// Warning: setting options such as tex_inv_y for textures that have already been loaded will have no effect!
	else if (str == "side_tid"    ) {global_building_params.cur_mat.side_tex.tid     = read_building_texture(fp, str, error);}
	else if (str == "side_nm_tid" ) {global_building_params.cur_mat.side_tex.nm_tid  = read_building_texture(fp, str, error);}
	else if (str == "roof_tid"    ) {global_building_params.cur_mat.roof_tex.tid     = read_building_texture(fp, str, error);}
	else if (str == "roof_nm_tid" ) {global_building_params.cur_mat.roof_tex.nm_tid  = read_building_texture(fp, str, error);}
	else if (str == "wall_tid"    ) {global_building_params.cur_mat.wall_tex.tid     = read_building_texture(fp, str, error);}
	else if (str == "wall_nm_tid" ) {global_building_params.cur_mat.wall_tex.nm_tid  = read_building_texture(fp, str, error);}
	else if (str == "floor_tid"   ) {global_building_params.cur_mat.floor_tex.tid    = read_building_texture(fp, str, error);}
	else if (str == "floor_nm_tid") {global_building_params.cur_mat.floor_tex.nm_tid = read_building_texture(fp, str, error);}
	else if (str == "ceil_tid"    ) {global_building_params.cur_mat.ceil_tex.tid     = read_building_texture(fp, str, error);}
	else if (str == "ceil_nm_tid" ) {global_building_params.cur_mat.ceil_tex.nm_tid  = read_building_texture(fp, str, error);}
	// material colors
	else if (str == "side_color") {
		if (!read_color(fp, global_building_params.cur_mat.side_color.cmin)) {buildings_file_err(str, error);}
		global_building_params.cur_mat.side_color.cmax = global_building_params.cur_mat.side_color.cmin; // same
	}
	else if (str == "side_color_min") {
		if (!read_color(fp, global_building_params.cur_mat.side_color.cmin)) {buildings_file_err(str, error);}
	}
	else if (str == "side_color_max") {
		if (!read_color(fp, global_building_params.cur_mat.side_color.cmax)) {buildings_file_err(str, error);}
	}
	else if (str == "side_color_grayscale_rand") {
		if (!read_float(fp, global_building_params.cur_mat.side_color.grayscale_rand)) {buildings_file_err(str, error);}
	}
	else if (str == "roof_color") {
		if (!read_color(fp, global_building_params.cur_mat.roof_color.cmin)) {buildings_file_err(str, error);}
		global_building_params.cur_mat.roof_color.cmax = global_building_params.cur_mat.roof_color.cmin; // same
	}
	else if (str == "roof_color_min") {
		if (!read_color(fp, global_building_params.cur_mat.roof_color.cmin)) {buildings_file_err(str, error);}
	}
	else if (str == "roof_color_max") {
		if (!read_color(fp, global_building_params.cur_mat.roof_color.cmax)) {buildings_file_err(str, error);}
	}
	else if (str == "roof_color_grayscale_rand") {
		if (!read_float(fp, global_building_params.cur_mat.roof_color.grayscale_rand)) {buildings_file_err(str, error);}
	}
	// windows
	else if (str == "window_width") {
		if (!read_float(fp, global_building_params.window_width) || !check_01(global_building_params.window_width)) {buildings_file_err(str, error);}
	}
	else if (str == "window_height") {
		if (!read_float(fp, global_building_params.window_height) || !check_01(global_building_params.window_height)) {buildings_file_err(str, error);}
	}
	else if (str == "window_xspace") {
		if (!read_float(fp, global_building_params.window_xspace) || !check_01(global_building_params.window_xspace)) {buildings_file_err(str, error);}
	}
	else if (str == "window_yspace") {
		if (!read_float(fp, global_building_params.window_yspace) || !check_01(global_building_params.window_yspace)) {buildings_file_err(str, error);}
	}
	else if (str == "window_xscale") {
		if (!read_float(fp, global_building_params.cur_mat.wind_xscale) || global_building_params.cur_mat.wind_xscale < 0.0) {buildings_file_err(str, error);}
	}
	else if (str == "window_yscale") {
		if (!read_float(fp, global_building_params.cur_mat.wind_yscale) || global_building_params.cur_mat.wind_yscale < 0.0) {buildings_file_err(str, error);}
	}
	else if (str == "window_xoff") {
		if (!read_float(fp, global_building_params.cur_mat.wind_xoff)) {buildings_file_err(str, error);}
	}
	else if (str == "window_yoff") {
		if (!read_float(fp, global_building_params.cur_mat.wind_yoff)) {buildings_file_err(str, error);}
		global_building_params.cur_mat.wind_yoff *= -1.0; // invert Y
	}
	else if (str == "wall_split_thresh") {
		if (!read_float(fp, global_building_params.wall_split_thresh)) {buildings_file_err(str, error);}
	}
	else if (str == "add_windows") { // per-material
		if (!read_bool(fp, global_building_params.cur_mat.add_windows)) {buildings_file_err(str, error);}
	}
	else if (str == "add_window_lights") { // per-material
		if (!read_bool(fp, global_building_params.cur_mat.add_wind_lights)) {buildings_file_err(str, error);}
	}
	else if (str == "house_prob") { // per-material
		if (!read_float(fp, global_building_params.cur_mat.house_prob)) {buildings_file_err(str, error);}
	}
	else if (str == "house_scale_range") { // per-material
		if (!read_float(fp, global_building_params.cur_mat.house_scale_min) || !read_float(fp, global_building_params.cur_mat.house_scale_max)) {buildings_file_err(str, error);}
	}
	else if (str == "window_color") { // per-material
		if (!read_color(fp, global_building_params.cur_mat.window_color)) {buildings_file_err(str, error);}
	}
	else if (str == "wall_color") { // per-material
		if (!read_color(fp, global_building_params.cur_mat.wall_color)) {buildings_file_err(str, error);}
	}
	else if (str == "ceil_color") { // per-material
		if (!read_color(fp, global_building_params.cur_mat.ceil_color)) {buildings_file_err(str, error);}
	}
	else if (str == "floor_color") { // per-material
		if (!read_color(fp, global_building_params.cur_mat.floor_color)) {buildings_file_err(str, error);}
	}
	// special commands
	else if (str == "probability") {
		if (!read_uint(fp, global_building_params.cur_prob)) {buildings_file_err(str, error);}
	}
	else if (str == "add_material") {global_building_params.add_cur_mat();}
	else {
		cout << "Unrecognized buildings keyword in input file: " << str << endl;
		error = 1;
	}
	return !error;
}


void do_xy_rotate(float rot_sin, float rot_cos, point const &center, point &pos) {
	float const x(pos.x - center.x), y(pos.y - center.y); // translate to center
	pos.x = x*rot_cos - y*rot_sin + center.x;
	pos.y = y*rot_cos + x*rot_sin + center.y;
}
void do_xy_rotate_normal(float rot_sin, float rot_cos, point &n) {
	float const x(n.x), y(n.y);
	n.x = x*rot_cos - y*rot_sin;
	n.y = y*rot_cos + x*rot_sin;
}


class building_window_gen_t { // and doors?
	int window_tid, hdoor_tid, bdoor_tid;
public:
	building_window_gen_t() : window_tid(-1), hdoor_tid(-1), bdoor_tid(-1) {}
	int get_window_tid() const {return window_tid;}
	
	int get_hdoor_tid() { // house door
		if (hdoor_tid < 0) {hdoor_tid = get_texture_by_name("textures/white_door.jpg");}
		if (hdoor_tid < 0) {hdoor_tid = WHITE_TEX;} // failed to load door texture - use a simple white texture
		return hdoor_tid;
	}
	int get_bdoor_tid() { // building door
		if (bdoor_tid < 0) {bdoor_tid = get_texture_by_name("textures/buildings/building_door.jpg");}
		if (bdoor_tid < 0) {bdoor_tid = WHITE_TEX;} // failed to load door texture - use a simple white texture
		return bdoor_tid;
	}
	bool check_windows_texture() {
		if (!global_building_params.windows_enabled()) return 0;
		if (window_tid >= 0) return 1; // already generated
		gen_building_window_texture(global_building_params.get_window_width_fract(), global_building_params.get_window_height_fract());
		window_tid = BLDG_WINDOW_TEX;
		return 1;
	}
};
building_window_gen_t building_window_gen;


class texture_id_mapper_t {
	vector<unsigned> tid_to_slot_ix;
	unsigned next_slot_ix;

	void register_tid(int tid) {
		if (tid < 0) return; // not allocated
		if (tid >= (int)tid_to_slot_ix.size()) {tid_to_slot_ix.resize(tid+1, 0);}
		if (tid_to_slot_ix[tid] == 0) {tid_to_slot_ix[tid] = next_slot_ix++;}
	}
public:
	texture_id_mapper_t() : next_slot_ix(1) {} // slots start at 1; slot 0 is for untextured

	void init() {
		if (!tid_to_slot_ix.empty()) return; // already inited
		tid_to_slot_ix.push_back(0); // untextured case
		register_tid(building_window_gen.get_window_tid());
		register_tid(building_window_gen.get_hdoor_tid());
		register_tid(building_window_gen.get_bdoor_tid());

		for (auto i = global_building_params.materials.begin(); i != global_building_params.materials.end(); ++i) {
			register_tid(i->side_tex.tid);
			register_tid(i->roof_tex.tid);
			register_tid(i->wall_tex.tid);
			register_tid(i->ceil_tex.tid);
			register_tid(i->floor_tex.tid);
		}
		cout << "Used " << (next_slot_ix-1) << " slots for texture IDs up to " << (tid_to_slot_ix.size()-1) << endl;
	}
	unsigned get_slot_ix(int tid) const {
		if (tid < 0) return 0; // untextured - slot 0
		assert(tid < (int)tid_to_slot_ix.size());
		return tid_to_slot_ix[tid];
	}
};
texture_id_mapper_t tid_mapper;


void building_room_geom_t::create_vbo() {
	if (cubes.empty())   return; // no geom
	if (vbo.vbo_valid()) return; // already created
	vector<vert_norm_comp_color> verts; // okay to use norm_comp here because all normals components are either -1 or +1
	verts.reserve(24*cubes.size()); // upper bound, assuming all faces of all cubes are drawn (skip_faces==0)

	for (auto c = cubes.begin(); c != cubes.end(); ++c) {
		vert_norm_comp_color v;
		v.copy_color(c->cw);

		// Note: stolen from draw_cube() with tex coord logic, back face culling, etc. removed
		for (unsigned i = 0; i < 3; ++i) { // iterate over dimensions
			unsigned const d[2] = {i, ((i+1)%3)}, n((i+2)%3);

			for (unsigned j = 0; j < 2; ++j) { // iterate over opposing sides, min then max
				if (c->skip_faces & (1 << (2*i + j))) continue; // skip this face
				v.set_ortho_norm(i, j);
				v.v[n] = j;

				for (unsigned s1 = 0; s1 < 2; ++s1) {
					v.v[d[1]] = c->d[d[1]][s1];

					for (unsigned k = 0; k < 2; ++k) { // iterate over vertices
						v.v[d[0]] = c->d[d[0]][k^j^s1^1]; // need to orient the vertices differently for each side
						verts.push_back(v);
					}
				}
			} // for j
		} // for i
	} // for c
	num_verts = verts.size();
}

void building_room_geom_t::draw() const {
	if (cubes.empty()) return; // no geom
	assert(vbo.vbo_valid());
	assert(num_verts > 0);
	vbo.pre_render();
	vert_norm_comp_color::set_vbo_arrays();
	draw_quads_as_tris(num_verts);
	vbo.post_render(); // move this out of the loop?
}

void building_interior_t::clear() {
	floors.clear();
	ceilings.clear();
	walls[0].clear();
	walls[1].clear();
	if (room_geom) {room_geom->clear(); room_geom.reset();}
}


/*static*/ void building_draw_utils::calc_normals(building_geom_t const &bg, vector<vector3d> &nv, unsigned ndiv) {

	assert(bg.flat_side_amt >= 0.0 && bg.flat_side_amt < 0.5); // generates a flat side
	assert(bg.alt_step_factor >= 0.0 && bg.alt_step_factor < 1.0);
	if (bg.flat_side_amt > 0.0) {assert(ndiv > 4);} // should be at least 5 sides, 6-8 is better
	float const ndiv_inv(1.0/ndiv), css(TWO_PI*ndiv_inv*(1.0f - bg.flat_side_amt));
	float sin_ds[2], cos_ds[2];

	if (bg.alt_step_factor > 0.0) { // alternate between large and small steps (cube with angled corners, etc.)
		assert(!(ndiv&1));
		float const css_v[2] = {css*(1.0f + bg.alt_step_factor), css*(1.0f - bg.alt_step_factor)};
		UNROLL_2X(sin_ds[i_] = sin(css_v[i_]); cos_ds[i_] = cos(css_v[i_]);)
	}
	else { // uniform side length
		sin_ds[0] = sin_ds[1] = sin(css);
		cos_ds[0] = cos_ds[1] = cos(css);
	}
	float sin_s(0.0), cos_s(1.0), angle0(bg.start_angle); // start at 0.0
	if (bg.half_offset) {angle0 = 0.5*css;} // for cube
	if (angle0 != 0.0) {sin_s = sin(angle0); cos_s = cos(angle0);} // uncommon case
	nv.resize(ndiv);

	for (unsigned S = 0; S < ndiv; ++S) { // build normals table
		bool const d(S&1);
		float const s(sin_s), c(cos_s);
		nv[S].assign(s, c, 0.0);
		sin_s = s*cos_ds[d] + c*sin_ds[d];
		cos_s = c*cos_ds[d] - s*sin_ds[d];
	}
}

/*static*/ void building_draw_utils::calc_poly_pts(building_geom_t const &bg, cube_t const &bcube, vector<point> &pts, float expand) {

	calc_normals(bg, pts, bg.num_sides);
	vector3d const sz(bcube.get_size());
	point const cc(bcube.get_cube_center());
	float const rx(0.5*sz.x + expand), ry(0.5*sz.y + expand); // expand polygon by sphere radius
	for (unsigned i = 0; i < bg.num_sides; ++i) {pts[i].assign((cc.x + rx*pts[i].x), (cc.y + ry*pts[i].y), 0.0);} // convert normals to points
}


#define EMIT_VERTEX() \
	vert.v = pt*sz + llc; \
	vert.t[ st] = tscale[ st]*(vert.v[d] + tex_vert_off[d]); \
	vert.t[!st] = tscale[!st]*(vert.v[i] + tex_vert_off[i]); \
	vert.t[0] += tex.txoff; \
	vert.t[1] += tex.tyoff; \
	if (apply_ao) {vert.copy_color(cw[pt.z == 1]);} \
	if (bg.is_rotated()) {do_xy_rotate(bg.rot_sin, bg.rot_cos, center, vert.v);} \
	verts.push_back(vert);

typedef vector<vert_norm_comp_tc_color> vect_vnctcc_t;

class building_draw_t {

	class draw_block_t {
		struct vert_ix_pair {
			unsigned qix, tix; // {quads, tris}
			vert_ix_pair(unsigned qix_, unsigned tix_) : qix(qix_), tix(tix_) {}
			bool operator==(vert_ix_pair const &v) const {return (qix == v.qix && tix == v.tix);}
		};
		// Note: not easy to use vao_manager_t due to upload done before active shader + shadow vs. geometry pass, but we can use vao_wrap_t's directly
		vbo_wrap_t vbo;
		vao_wrap_t vao, svao; // regular + shadow
		vector<vert_ix_pair> pos_by_tile; // {quads, tris}
		unsigned tri_vbo_off;
	public:
		tid_nm_pair_t tex;
		vect_vnctcc_t quad_verts, tri_verts;

		draw_block_t() : tri_vbo_off(0) {}

		void draw_geom_range(bool shadow_only, vert_ix_pair const &vstart, vert_ix_pair const &vend) { // use VBO rendering
			if (vstart == vend) return; // empty range - no verts for this tile
			if (!shadow_only) {tex.set_gl();}
			assert(vbo.vbo_valid());
			(shadow_only ? svao : vao).create_from_vbo<vert_norm_comp_tc_color>(vbo, 1, 1); // setup_pointers=1, always_bind=1

			if (vstart.qix != vend.qix) {
				assert(vstart.qix < vend.qix);
				draw_quads_as_tris((vend.qix - vstart.qix), vstart.qix);
			}
			if (vstart.tix != vend.tix) {
				assert(vstart.tix < vend.tix);
				glDrawArrays(GL_TRIANGLES, (vstart.tix + tri_vbo_off), (vend.tix - vstart.tix));
			}
			vao_manager_t::post_render();
		}
		void draw_all_geom(bool shadow_only) {
			if (pos_by_tile.empty()) return; // nothing to draw for this block/texture
			draw_geom_range(shadow_only, pos_by_tile.front(), pos_by_tile.back());
		}
		void draw_geom_tile(unsigned tile_id) {
			if (pos_by_tile.empty()) return; // nothing to draw for this block/texture
			assert(tile_id+1 < pos_by_tile.size()); // tile and next tile must be valid indices
			draw_geom_range(0, pos_by_tile[tile_id], pos_by_tile[tile_id+1]); // shadow_only=0
		}
		void upload_to_vbos() {
			assert((quad_verts.size()%4) == 0);
			assert((tri_verts.size()%3) == 0);
			tri_vbo_off = quad_verts.size(); // triangles start after quads
			quad_verts.insert(quad_verts.end(), tri_verts.begin(), tri_verts.end()); // add tri_verts to quad_verts
			clear_cont(tri_verts); // no longer needed
			if (!quad_verts.empty()) {vbo.create_and_upload(quad_verts, 0, 1);}
			clear_cont(quad_verts); // no longer needed
		}
		void register_tile_id(unsigned tid) {
			if (tid+1 == pos_by_tile.size()) return; // already saw this tile
			assert(tid >= pos_by_tile.size()); // tid must be strictly increasing
			pos_by_tile.resize(tid+1, vert_ix_pair(quad_verts.size(), tri_verts.size())); // push start of new range back onto all previous tile slots
		}
		void finalize(unsigned num_tiles) {
			if (pos_by_tile.empty()) return; // nothing to do
			register_tile_id(num_tiles); // add terminator
			remove_excess_cap(pos_by_tile);
		}
		void clear_verts() {quad_verts.clear(); tri_verts.clear(); pos_by_tile.clear();}
		void clear_vbos () {vbo.clear(); vao.clear(); svao.clear();}
		void clear() {clear_vbos(); clear_verts();}
		bool empty() const {return (quad_verts.empty() && tri_verts.empty());}
		bool has_drawn() const {return !pos_by_tile.empty();}
		unsigned num_verts() const {return (quad_verts.size() + tri_verts.size());}
		unsigned num_tris () const {return (quad_verts.size()/2 + tri_verts.size()/3);} // Note: 1 quad = 4 verts = 2 triangles
	}; // end draw_block_t
	vector<draw_block_t> to_draw; // one per texture, assumes tids are dense

	vect_vnctcc_t &get_verts(tid_nm_pair_t const &tex, bool quads_or_tris=0) { // default is quads
		unsigned const ix(tid_mapper.get_slot_ix(tex.tid));
		if (ix >= to_draw.size()) {to_draw.resize(ix+1);}
		draw_block_t &block(to_draw[ix]);
		block.register_tile_id(cur_tile_id);
		if (block.empty()) {block.tex = tex;} // copy material first time
		else {
			assert(block.tex.tid == tex.tid);
			if (block.tex.nm_tid != tex.nm_tid) { // else normal maps must agree
				std::cerr << "mismatched normal map for texture ID " << block.tex.tid << " in slot " << ix << ": " << block.tex.nm_tid << " vs. " << tex.nm_tid << endl;
				assert(0);
			}
		}
		return (quads_or_tris ? block.tri_verts : block.quad_verts);
	}
	static void setup_ao_color(colorRGBA const &color, float bcz1, float ao_bcz2, float z1, float z2, color_wrapper cw[2], vert_norm_comp_tc_color &vert, bool no_ao) {
		if (!no_ao && global_building_params.ao_factor > 0.0) {
			float const dz_mult(global_building_params.ao_factor/(ao_bcz2 - bcz1));
			UNROLL_2X(cw[i_].set_c4(color*((1.0f - global_building_params.ao_factor) + dz_mult*((i_ ? z2 : z1) - bcz1)));)
		} else {vert.set_c4(color);} // color is shared across all verts
	}
	vector<vector3d> normals; // reused across add_cylinder() calls
	point cur_camera_pos;

public:
	unsigned cur_tile_id;
	building_draw_t() : cur_camera_pos(zero_vector), cur_tile_id(0) {}
	void init_draw_frame() {cur_camera_pos = get_camera_pos();} // capture camera pos during non-shadow pass to use for shadow pass
	bool empty() const {return to_draw.empty();}
	void reserve_verts(tid_nm_pair_t const &tex, size_t num, bool quads_or_tris=0) {get_verts(tex, quads_or_tris).reserve(num);}

	void toggle_transparent_windows_mode() {
		for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {i->tex.toggle_transparent_windows_mode();}
	}
	void add_cylinder(building_geom_t const &bg, point const &pos, point const &rot_center, float height, float rx, float ry, float bcz1, float ao_bcz2,
		tid_nm_pair_t const &tex, colorRGBA const &color, unsigned dim_mask, bool no_ao, bool clip_windows)
	{
		unsigned const ndiv(bg.num_sides); // Note: no LOD
		assert(ndiv >= 3);
		bool const smooth_normals(ndiv >= 16); // cylinder vs. N-gon
		float const z_top(pos.z + height), tscale_x(2.0*tex.tscale_x), tscale_y(2.0*tex.tscale_y); // adjust for local vs. global space change
		bool const apply_ao(!no_ao && global_building_params.ao_factor > 0.0);
		vert_norm_comp_tc_color vert;
		color_wrapper cw[2];
		setup_ao_color(color, bcz1, ao_bcz2, pos.z, z_top, cw, vert, no_ao);
		float tex_pos[2] = {0.0, 1.0};
		building_draw_utils::calc_normals(bg, normals, ndiv);
		UNROLL_2X(tex_pos[i_] = ((i_ ? z_top : pos.z) - bcz1);)

		if (dim_mask & 3) { // draw sides
			auto &verts(get_verts(tex)); // Note: cubes are drawn with quads, so we want to emit quads here
			float tot_perim(0.0), cur_perim[2] = {0.0, 0.0};
			for (unsigned S = 0; S < ndiv; ++S) {tot_perim += p2p_dist(normals[S], normals[(S+1)%ndiv]);}
			float const tscale_mult(TWO_PI*sqrt((rx*rx + ry*ry)/2.0f)/tot_perim);
				
			for (unsigned S = 0; S < ndiv; ++S) { // generate vertex data quads
				vector3d const &n1(normals[S]), &n2(normals[(S+1)%ndiv]);
				cur_perim[0]  = cur_perim[1];
				cur_perim[1] += p2p_dist(n1, n2);
				vector3d normal(n1 + n2); normal.x *= ry; normal.y *= rx; // average the two vertex normals for the flat face normal
				if (bg.is_rotated()) {do_xy_rotate_normal(bg.rot_sin, bg.rot_cos, normal);}
				bool const cur_smooth_normals(smooth_normals && (bg.flat_side_amt == 0.0 || S+1 != ndiv)); // flat side of cylindrical building is not smooth
				if (!cur_smooth_normals) {vert.set_norm(normal.get_norm());}

				for (unsigned d = 0; d < 2; ++d) {
					vector3d const &n(d ? n2 : n1);
					vert.t[0] = tscale_x*cur_perim[d]*tscale_mult + tex.txoff; // Note: could try harder to ensure an integer multiple to fix seams, but not a problem in practice
					
					if (cur_smooth_normals) {
						vector3d normal(n); normal.x *= ry; normal.y *= rx; // scale normal by radius (swapped)
						if (bg.is_rotated()) {do_xy_rotate_normal(bg.rot_sin, bg.rot_cos, normal);}
						vert.set_norm(normal.get_norm());
					}
					vert.v.assign((pos.x + rx*n.x), (pos.y + ry*n.y), 0.0);
					if (bg.is_rotated()) {do_xy_rotate(bg.rot_sin, bg.rot_cos, rot_center, vert.v);}

					for (unsigned e = 0; e < 2; ++e) {
						vert.v.z = ((d^e) ? z_top : pos.z);
						vert.t[1] = tscale_y*tex_pos[d^e] + tex.tyoff;
						if (apply_ao) {vert.copy_color(cw[d^e]);}
						verts.push_back(vert);
					}
					if (clip_windows) {clip_low_high(verts[verts.size()-1].t[1], verts[verts.size()-2].t[1]);} // is this necessary?
				} // for d
			} // for S
		} // end draw sides
		if (dim_mask & 4) { // draw end(s) / roof
			auto &tri_verts(get_verts(tex, 1));
			
			for (unsigned d = 0; d < 2; ++d) { // bottom, top
				vert.set_ortho_norm(2, d); // +/- z
				if (apply_ao) {vert.copy_color(cw[d]);}
				vert_norm_comp_tc_color center(vert);
				center.t[0] = center.t[1] = 0.0; // center of texture space for this disk
				center.v = pos;
				if (d) {center.v.z += height;}
				if (bg.is_rotated()) {do_xy_rotate(bg.rot_sin, bg.rot_cos, rot_center, center.v);}

				for (unsigned S = 0; S < ndiv; ++S) { // generate vertex data triangles
					tri_verts.push_back(center);

					for (unsigned e = 0; e < 2; ++e) {
						if (S > 0 && e == 0) {tri_verts.push_back(tri_verts[tri_verts.size()-2]); continue;} // reuse prev vertex
						vector3d const &n(normals[(S+e)%ndiv]);
						vert.v.assign((pos.x + rx*n.x), (pos.y + ry*n.y), center.v.z);
						vert.t[0] = tscale_x*n[0]; vert.t[1] = tscale_y*n[1];
						if (bg.is_rotated()) {do_xy_rotate(bg.rot_sin, bg.rot_cos, rot_center, vert.v);}
						tri_verts.push_back(vert);
					}
				} // for S
			} // for d
		} // end draw end(s)
	}

	void add_tquad(building_geom_t const &bg, tquad_with_ix_t const &tquad, cube_t const &bcube, tid_nm_pair_t const &tex, colorRGBA const &color) {
		assert(tquad.npts == 3 || tquad.npts == 4); // triangles or quads
		auto &verts(get_verts(tex, (tquad.npts == 3))); // 0=quads, 1=tris
		point const center(!bg.is_rotated() ? all_zeros : bcube.get_cube_center()); // rotate about bounding cube / building center
		vert_norm_comp_tc_color vert;
		float tsx(0.0), tsy(0.0), tex_off(0.0);
		bool dim(0);

		if (tquad.type == tquad_with_ix_t::TYPE_WALL) { // side/wall
			tsx = 2.0f*tex.tscale_x; tsy = 2.0f*tex.tscale_y; // adjust for local vs. global space change
			dim = (tquad.pts[0].x == tquad.pts[1].x);
			if (world_mode != WMODE_INF_TERRAIN) {tex_off = (dim ? yoff2*DY_VAL : xoff2*DX_VAL);}
		}
		else if (tquad.type == tquad_with_ix_t::TYPE_ROOF || tquad.type == tquad_with_ix_t::TYPE_CCAP) { // roof or chimney cap
			float const denom(0.5f*(bcube.get_dx() + bcube.get_dy()));
			tsx = tex.tscale_x/denom; tsy = tex.tscale_y/denom;
		}
		vert.set_c4(color);
		vector3d normal(tquad.get_norm());
		if (bg.is_rotated()) {do_xy_rotate_normal(bg.rot_sin, bg.rot_cos, normal);}
		vert.set_norm(normal);
		
		for (unsigned i = 0; i < tquad.npts; ++i) {
			vert.v = tquad.pts[i];

			if (tquad.type == tquad_with_ix_t::TYPE_WALL) { // side/wall
				vert.t[0] = (vert.v[dim] + tex_off)*tsx; // use nonzero width dim
				vert.t[1] = (vert.v.z - bcube.z1())*tsy;
			}
			else if (tquad.type == tquad_with_ix_t::TYPE_ROOF || tquad.type == tquad_with_ix_t::TYPE_CCAP) { // roof or chimney cap
				vert.t[0] = (vert.v.x - bcube.x1())*tsx; // varies from 0.0 and bcube x1 to 1.0 and bcube x2
				vert.t[1] = (vert.v.y - bcube.y1())*tsy; // varies from 0.0 and bcube y1 to 1.0 and bcube y2
			}
			else if (tquad.type == tquad_with_ix_t::TYPE_HDOOR || tquad.type == tquad_with_ix_t::TYPE_BDOOR) { // door - textured from (0,0) to (1,1)
				vert.t[0] = float(i == 1 || i == 2);
				vert.t[1] = float(i == 2 || i == 3);
			}
			else {assert(0);}
			if (bg.is_rotated()) {do_xy_rotate(bg.rot_sin, bg.rot_cos, center, vert.v);}
			verts.push_back(vert);
		}
	}

	// clip_windows: 0=no clip, 1=clip for building, 2=clip for house
	void add_section(building_geom_t const &bg, vect_cube_t const &parts, cube_t const &cube, cube_t const &bcube, float ao_bcz2, tid_nm_pair_t const &tex,
		colorRGBA const &color, unsigned dim_mask, bool skip_bottom, bool skip_top, bool no_ao, int clip_windows, float door_ztop=0.0, unsigned door_sides=0)
	{
		assert(bg.num_sides >= 3); // must be nonzero volume
		point const center(!bg.is_rotated() ? all_zeros : bcube.get_cube_center()); // rotate about bounding cube / building center
		vector3d const sz(cube.get_size());

		if (bg.num_sides != 4) { // not a cube, use cylinder
			assert(door_ztop == 0.0); // not supported
			point const ccenter(cube.get_cube_center()), pos(ccenter.x, ccenter.y, cube.z1());
			//float const rscale(0.5*((num_sides <= 8) ? SQRT2 : 1.0)); // larger for triangles/cubes/hexagons/octagons (to ensure overlap/connectivity), smaller for cylinders
			float const rscale(0.5); // use shape contained in bcube so that bcube tests are correct, since we're not creating L/T/U shapes for this case
			add_cylinder(bg, pos, center, sz.z, rscale*sz.x, rscale*sz.y, bcube.z1(), ao_bcz2, tex, color, dim_mask, no_ao, clip_windows);
			return;
		}
		// else draw as a cube (optimized flow)
		auto &verts(get_verts(tex, bg.is_pointed)); // bg.is_pointed ? tris : quads
		vector3d const llc(cube.get_llc()); // move origin from center to min corner
		vert_norm_comp_tc_color vert;
		if (bg.is_pointed) {dim_mask &= 3;} // mask off z-dim since pointed objects (antenna) have no horizontal surfaces
		float const tscale[2] = {2.0f*tex.tscale_x, 2.0f*tex.tscale_y}; // adjust for local vs. global space change
		bool const apply_ao(!no_ao && global_building_params.ao_factor > 0.0);
		color_wrapper cw[2];
		setup_ao_color(color, bcube.z1(), ao_bcz2, cube.d[2][0], cube.d[2][1], cw, vert, no_ao);
		vector3d tex_vert_off(((world_mode == WMODE_INF_TERRAIN) ? zero_vector : vector3d(xoff2*DX_VAL, yoff2*DY_VAL, 0.0)));
		tex_vert_off.z = -bcube.z1();
		
		for (unsigned i = 0; i < 3; ++i) { // iterate over dimensions
			unsigned const n((i+2)%3), d((i+1)%3), st(i&1); // n = dim of normal, i/d = other dims
			if (!(dim_mask & (1<<n))) continue;

			for (unsigned j = 0; j < 2; ++j) { // iterate over opposing sides, min then max
				if (skip_bottom && n == 2 && j == 0) continue; // skip bottom side
				if (skip_top    && n == 2 && j == 1) continue; // skip top    side
				
				if (n < 2 && bg.is_rotated()) { // XY only
					vector3d norm; norm.z = 0.0;
					if (n == 0) {norm.x =  bg.rot_cos; norm.y = bg.rot_sin;} // X
					else        {norm.x = -bg.rot_sin; norm.y = bg.rot_cos;} // Y
					vert.set_norm(j ? norm : -norm);
				}
				else {
					vert.n[i] = 0; vert.n[d] = 0; vert.n[n] = (j ? 127 : -128); // -1.0 or 1.0
				}
				point pt; // parameteric position within cube in [vec3(0), vec3(1)]
				pt[n] = j; // our cube face, in direction of normal

				if (bg.is_pointed) { // antenna triangle; parts clipping doesn't apply to this case since there are no opposing cube faces
					unsigned const ix(verts.size()); // first vertex of this triangle
					assert(door_ztop == 0.0); // not supported
					pt[!n] = !j; pt.z = 0;
					EMIT_VERTEX(); // bottom low
					pt[!n] = j;
					EMIT_VERTEX(); // bottom high
					pt[!n] = 0.5; pt[n] = 0.5; pt.z = 1;
					EMIT_VERTEX(); // top
					vector3d normal;
					get_normal(verts[ix+0].v, verts[ix+1].v, verts[ix+2].v, normal, 1); // update with correct normal
					vert.set_norm(n ? -normal : normal);
					UNROLL_3X(verts[ix+i_].set_norm(vert);)
					continue; // no windows/clipping
				}
				struct wall_seg_t {
					float dlo, dhi, ilo, ihi;
					bool enabled;
					wall_seg_t() : dlo(0), dhi(1), ilo(0), ihi(1), enabled(0) {}
					
					void finalize() {
						if (dlo >= dhi || ilo >= ihi) {enabled = 0;} // clipped to zero area (can happen in buildings with overlapping cubes)
						assert(dlo >= 0.0f && dhi <= 1.0f && ilo >= 0.0f && ihi <= 1.0f);
					}
				};
				wall_seg_t segs[3]; // lo, hi, top
				segs[0].enabled = 1; // default is first segment used only

				if (ADD_BUILDING_INTERIORS && !parts.empty() && n != 2) { // clip walls XY to remove intersections; this applies to both walls and windows
					unsigned const xy(1 - n); // non-Z parameteric dim (the one we're clipping)
					float &clo1((d == xy) ? segs[0].dlo : segs[0].ilo), &chi1((d == xy) ? segs[0].dhi : segs[0].ihi); // clip dim values (first  seg)
					float &clo2((d == xy) ? segs[1].dlo : segs[1].ilo); // clip dim values (second seg)
					float const face_val(cube.d[n][j]);

					// Note: in general we shouldn't compare floats with ==, but in this case we know the values have been directly assigned so they really should be equal
					for (auto p = parts.begin(); p != parts.end(); ++p) {
						if (*p == cube) continue; // skip ourself
						if (p->d[n][!j] != face_val && (p->d[n][0] >= face_val || p->d[n][1] <= face_val)) continue; // face not contained in dir of normal (inc opposing aligned val)
						float const pxy1(p->d[xy][0]), pxy2(p->d[xy][1]), cxy1(cube.d[xy][0]), cxy2(cube.d[xy][1]); // end points used for clipping
						if (pxy2 <= cxy1 || pxy1 >= cxy2) continue; // no overlap in XY dim
						if (p->z2() <= cube.z1() || cube.z2() <= p->z1()) continue; // no overlap in Z
						// not sure if this can actually happen, will handle it if it does; it would apply to porch roofs without the edge adjustment hack
						// doesn't apply to windows (partial height walls but not windows)
						if (!clip_windows && p->z1() > cube.z1()) continue; // opposing cube doesn't cover this cube in Z (floor too high)

						if (p->z2() < cube.z2()) { // opposing cube doesn't cover this cube in Z (ceiling too low); this should only happen for one part
							if (segs[2].enabled) continue; // already have a Z segment - ignore split (can this happen?)
							// don't copy/enable the top segment for house windows because houses always have a sloped roof section on top that will block the windows
							if (clip_windows != 2) {segs[2] = segs[0];} // copy from first segment (likely still [0,1]), will set enabled=1
							float const z_split((p->z2() - cube.z1())/sz.z); // parametric value of Z split point
							assert(z_split >= 0.0 && z_split <= 1.0);
							if (d == xy) {segs[0].ihi = segs[1].ihi = segs[2].ilo = z_split;} else {segs[0].dhi = segs[1].dhi = segs[2].dlo = z_split;} // adjust Z dim
						}
						bool const cov_lo(pxy1 <= cxy1), cov_hi(pxy2 >= cxy2);
						if (cov_lo && cov_hi) {segs[0].enabled = 0; continue;} // fully contained - drop
						// Note: we can get into the cov_lo and cov_hi cases for two different parts and both edges will be clipped
						if      (cov_lo) {clo1 = (pxy2 - cxy1)/sz[xy];} // clip on lo side
						else if (cov_hi) {chi1 = (pxy1 - cxy1)/sz[xy];} // clip on hi side
						else { // clip on both sides and emit two quads
							chi1 = (pxy1 - cxy1)/sz[xy]; // lo side, first  seg
							clo2 = (pxy2 - cxy1)/sz[xy]; // hi side, second seg
							assert(chi1 >= 0.0 && chi1 <= 1.0);
							assert(clo2 >= 0.0 && clo2 <= 1.0);
							segs[1].enabled = 1;
							break; // I don't think any current building types can have another adjacency, and it's difficult to handle, so stop here
						}
					} // for p
				} // end wall clipping
				for (unsigned s = 0; s < 3; ++s) {
					wall_seg_t &seg(segs[s]);
					seg.finalize();
					if (!seg.enabled) continue; // this segment unused
					unsigned const ix(verts.size()); // first vertex of this quad
					pt[d] = seg.dlo;
					pt[i] = (j ? seg.ilo : seg.ihi); // need to orient the vertices differently for each side
					//if (bg.roof_recess > 0.0 && n == 2 && j == 1) {pt.z -= bg.roof_recess*cube.get_dz();}
					EMIT_VERTEX(); // 0 !j
					pt[i] = (j ? seg.ihi : seg.ilo);
					EMIT_VERTEX(); // 0 j
					pt[d] = seg.dhi;
					EMIT_VERTEX(); // 1 j
					pt[i] = (j ? seg.ilo : seg.ihi);
					EMIT_VERTEX(); // 1 !j

					if (s < 2 && (door_sides & (1 << (2*n + j)))) { // clip zval to exclude door z-range (except for top segment)
						float const door_height(door_ztop - cube.z1()), offset(0.03*(j ? 1.0 : -1.0)*door_height);
						assert(door_height > 0.0);

						for (unsigned k = ix; k < ix+4; ++k) {
							auto &v(verts[k]);
							float const delta(door_ztop - v.v.z);
							max_eq(v.v.z, door_ztop); // make all windows start above the door; TODO: add partial row of windows for first floor along this wall
							v.v[n] += offset; // move slightly away from the house wall to avoid z-fighting (vertex is different from building and won't have same depth)
							if (delta > 0.0) {v.t[1] += tscale[1]*delta;} // recalculate tex coord
						}
					}
					else if (clip_windows && DRAW_WINDOWS_AS_HOLES) { // move slightly away from the house wall to avoid z-fighting
						float const offset(0.005*(j ? 1.0 : -1.0)*sz.z);
						for (unsigned k = ix; k < ix+4; ++k) {verts[k].v[n] += offset;}
					}
					if (clip_windows && n < 2) { // clip the quad that was just added (side of building)
						clip_low_high(verts[ix+0].t[!st], verts[ix+1].t[!st]);
						clip_low_high(verts[ix+2].t[!st], verts[ix+3].t[!st]);
						clip_low_high(verts[ix+0].t[ st], verts[ix+3].t[ st]);
						clip_low_high(verts[ix+1].t[ st], verts[ix+2].t[ st]);
					}
				} // for seg s
			} // for j
		} // for i
	}

	unsigned num_verts() const {
		unsigned num(0);
		for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {num += i->num_verts();}
		return num;
	}
	unsigned num_tris() const {
		unsigned num(0);
		for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {num += i->num_tris();}
		return num;
	}
	void upload_to_vbos() {for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {i->upload_to_vbos();}}
	void clear_vbos    () {for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {i->clear_vbos();}}
	void clear         () {for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {i->clear();}}
	unsigned get_num_draw_blocks() const {return to_draw.size();}
	void finalize(unsigned num_tiles) {for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {i->finalize(num_tiles);}}
	void draw          (bool shadow_only) {for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {i->draw_all_geom (shadow_only);}}
	void draw_tile     (unsigned tile_id) {for (auto i = to_draw.begin(); i != to_draw.end(); ++i) {i->draw_geom_tile(tile_id);}}
	void draw_block(unsigned ix, bool shadow_only) {if (ix < to_draw.size()) {to_draw[ix].draw_all_geom(shadow_only);}}
}; // end building_draw_t


// *** Drawing ***
void building_t::get_all_drawn_verts(building_draw_t &bdraw, bool get_exterior, bool get_interior) const {

	assert(get_exterior || get_interior); // must be at least one of these
	if (!is_valid()) return; // invalid building
	building_mat_t const &mat(get_material());

	if (get_exterior) { // exterior building parts
		for (auto i = parts.begin(); i != parts.end(); ++i) { // multiple cubes/parts/levels
			bdraw.add_section(*this, parts, *i, bcube, (is_house ? i->z2() : ao_bcz2), mat.side_tex, side_color, 3, 0, 0, 0, 0); // XY
			bool const skip_top(!roof_tquads.empty() && (is_house || i+1 == parts.end())); // don't add the flat roof for the top part in this case
			bool const is_stacked(!is_house && num_sides == 4 && i->d[2][0] > bcube.d[2][0]); // skip the bottom of stacked cubes
			if (is_stacked && skip_top) continue; // no top/bottom to draw
			bdraw.add_section(*this, parts, *i, bcube, (is_house ? i->z2() : ao_bcz2), mat.roof_tex, roof_color, 4, is_stacked, skip_top, 0, 0); // only Z dim
		}
		for (auto i = roof_tquads.begin(); i != roof_tquads.end(); ++i) {
			bool const is_wall_tex(i->type != tquad_with_ix_t::TYPE_ROOF);
			bdraw.add_tquad(*this, *i, bcube, (is_wall_tex ? mat.side_tex : mat.roof_tex), (is_wall_tex ? side_color : roof_color)); // use type to select roof vs. side texture
		}
		for (auto i = details.begin(); i != details.end(); ++i) { // draw roof details
			building_geom_t bg(4, rot_sin, rot_cos); // cube
			bg.is_pointed = (has_antenna && i+1 == details.end()); // draw antenna as a point
			bdraw.add_section(bg, vect_cube_t(), *i, bcube, ao_bcz2, mat.roof_tex.get_scaled_version(0.5),
				detail_color*(bg.is_pointed ? 0.5 : 1.0), 7, 1, 0, 1, 0); // all dims, skip_bottom, no AO
		}
		// doors are both interior and exterior so are always drawn; but maybe they should be drawn only in the exterior block to avoid drawing twice when both passes are enabled?
		for (auto i = doors.begin(); i != doors.end(); ++i) {
			int const door_tid((i->type == tquad_with_ix_t::TYPE_BDOOR) ? building_window_gen.get_bdoor_tid() : building_window_gen.get_hdoor_tid());
			bdraw.add_tquad(*this, *i, bcube, tid_nm_pair_t(door_tid, -1, 1.0, 1.0), WHITE);
		}
	}
	if (get_interior && interior != nullptr) { // interior building parts
		// should we defer this until the player is near/inside the building?
		// how do we skip drawing of the building exterior when the player is close to and enters the building?
		// make windows transparent?
		for (auto i = interior->floors.begin(); i != interior->floors.end(); ++i) {
			bdraw.add_section(*this, vect_cube_t(), *i, bcube, ao_bcz2, mat.floor_tex, mat.floor_color, 4, 1, 0, 1, 0); // no AO; skip_bottom; Z dim only (what about edges?)
		}
		for (auto i = interior->ceilings.begin(); i != interior->ceilings.end(); ++i) {
			bdraw.add_section(*this, vect_cube_t(), *i, bcube, ao_bcz2, mat.ceil_tex, mat.ceil_color, 4, 0, 1, 1, 0); // no AO; skip_top; Z dim only (what about edges?)
		}
		for (unsigned dim = 0; dim < 2; ++dim) { // Note: can almost pass in (1U << dim) as dim_filt, if it wasn't for door cutouts
			for (auto i = interior->walls[dim].begin(); i != interior->walls[dim].end(); ++i) {
				bdraw.add_section(*this, vect_cube_t(), *i, bcube, ao_bcz2, mat.wall_tex, mat.wall_color, 3, 0, 0, 1, 0); // no AO; X/Y dims only
			}
		}
	}
}

void building_t::get_all_drawn_window_verts(building_draw_t &bdraw, bool lights_pass) const {

	if (!is_valid() || !global_building_params.windows_enabled()) return; // invalid building or no windows
	building_mat_t const &mat(get_material());
	if (lights_pass ? !mat.add_wind_lights : !mat.add_windows) return; // no windows for this material
	int const window_tid(building_window_gen.get_window_tid());
	if (window_tid < 0) return; // not allocated - error?
	if (mat.wind_xscale == 0.0 || mat.wind_yscale == 0.0) return; // no windows for this material?
	tid_nm_pair_t tex(window_tid, -1, mat.get_window_tx(), mat.get_window_ty(), mat.wind_xoff, mat.wind_yoff);
	colorRGBA color;

	if (lights_pass) { // slight yellow-blue tinting using bcube x1/y1 as a hash
		float const tint(0.2*fract(100.0f*(bcube.x1() + bcube.y1())));
		color = colorRGBA((1.0 - tint), (1.0 - tint), (0.8 + tint), 1.0);
	} else {color = mat.window_color;}
	// only clip non-city windows; city building windows tend to be aligned with the building textures (maybe should be a material option?)
	int const clip_windows(mat.no_city ? (is_house ? 2 : 1) : 0);
	float const door_ztop(doors.empty() ? 0.0f : (EXACT_MULT_FLOOR_HEIGHT ? (bcube.z1() + mat.get_floor_spacing()) : doors.front().pts[2].z));

	for (auto i = parts.begin(); i != (parts.end() - has_chimney); ++i) { // multiple cubes/parts/levels, excluding chimney
		unsigned const part_ix(i - parts.begin());
		unsigned const dsides((part_ix < 4 && mat.add_windows) ? door_sides[part_ix] : 0); // skip windows on sides with doors, but only for buildings with windows
		bdraw.add_section(*this, parts, *i, bcube, ao_bcz2, tex, color, 3, 0, 0, 1, clip_windows, door_ztop, dsides); // XY, no_ao=1
	}
}


class building_creator_t {

	unsigned grid_sz, gpu_mem_usage;
	vector3d range_sz, range_sz_inv, max_extent;
	cube_t range, buildings_bcube;
	rand_gen_t rgen;
	vector<building_t> buildings;
	vector<vector<unsigned>> bix_by_plot; // cached for use with pedestrian collisions
	building_draw_t building_draw, building_draw_vbo, building_draw_windows, building_draw_wind_lights, building_draw_interior;
	point_sprite_drawer_sized building_lights;
	vector<point> points; // reused temporary
	bool use_smap_this_frame;

	struct grid_elem_t {
		vector<cube_with_ix_t> bc_ixs;
		cube_t bcube;
		grid_elem_t() : bcube(all_zeros) {}

		void add(cube_t const &c, unsigned ix) {
			if (bc_ixs.empty()) {bcube = c;} else {bcube.union_with_cube(c);}
			bc_ixs.emplace_back(c, ix);
		}
	};
	vector<grid_elem_t> grid, grid_by_tile;

	grid_elem_t &get_grid_elem(unsigned gx, unsigned gy) {
		assert(gx < grid_sz && gy < grid_sz);
		return grid[gy*grid_sz + gx];
	}
	grid_elem_t const &get_grid_elem(unsigned gx, unsigned gy) const {
		assert(gx < grid_sz && gy < grid_sz);
		return grid[gy*grid_sz + gx];
	}
	struct bix_by_x1 {
		vector<building_t> const &buildings;
		bix_by_x1(vector<building_t> const &buildings_) : buildings(buildings_) {}
		bool operator()(unsigned const a, unsigned const b) const {return (buildings[a].bcube.x1() < buildings[b].bcube.x1());}
	};
	unsigned get_grid_ix(point pos) const {
		range.clamp_pt_xy(pos);
		unsigned gxy[2];
		for (unsigned d = 0; d < 2; ++d) {
			float const v((pos[d] - range.d[d][0])*range_sz_inv[d]);
			gxy[d] = unsigned(v*(grid_sz-1));
			assert(gxy[d] < grid_sz);
		}
		return (gxy[1]*grid_sz + gxy[0]);
	}
	void get_grid_range(cube_t const &bcube, unsigned ixr[2][2]) const { // {lo,hi}x{x,y}
		point llc(bcube.get_llc()), urc(bcube.get_urc());
		range.clamp_pt_xy(llc);
		range.clamp_pt_xy(urc);
		for (unsigned d = 0; d < 2; ++d) {
			float const v1((llc[d] - range.d[d][0])*range_sz_inv[d]), v2((urc[d] - range.d[d][0])*range_sz_inv[d]);
			ixr[0][d] = unsigned(v1*(grid_sz-1));
			ixr[1][d] = unsigned(v2*(grid_sz-1));
			assert(ixr[0][d] < grid_sz && ixr[1][d] < grid_sz);
		}
	}
	void add_to_grid(cube_t const &bcube, unsigned bix) {
		unsigned ixr[2][2];
		get_grid_range(bcube, ixr);
		for (unsigned y = ixr[0][1]; y <= ixr[1][1]; ++y) {
			for (unsigned x = ixr[0][0]; x <= ixr[1][0]; ++x) {get_grid_elem(x, y).add(bcube, bix);}
		}
	}
	bool check_for_overlaps(vector<unsigned> const &ixs, cube_t const &test_bc, building_t const &b, float expand_rel, float expand_abs, vector<point> &points) const {
		for (auto i = ixs.begin(); i != ixs.end(); ++i) {
			building_t const &ob(get_building(*i));
			if (test_bc.intersects_xy(ob.bcube) && ob.check_bcube_overlap_xy(b, expand_rel, expand_abs, points)) return 1;
		}
		return 0;
	}
	bool check_for_overlaps(vector<cube_with_ix_t> const &bc_ixs, cube_t const &test_bc, building_t const &b, float expand_rel, float expand_abs, vector<point> &points) const {
		for (auto i = bc_ixs.begin(); i != bc_ixs.end(); ++i) {
			if (test_bc.intersects_xy(*i) && get_building(i->ix).check_bcube_overlap_xy(b, expand_rel, expand_abs, points)) return 1;
		}
		return 0;
	}

	void build_grid_by_tile(bool single_tile) {
		grid_by_tile.clear();

		if (single_tile || world_mode != WMODE_INF_TERRAIN) { // not used in this mode - add all buildings to the first tile
			grid_by_tile.resize(1);
			grid_by_tile.front().bc_ixs.reserve(buildings.size());
			for(unsigned bix = 0; bix < buildings.size(); ++bix) {grid_by_tile.front().add(buildings[bix].bcube, bix);}
			return;
		}
		//timer_t timer("build_grid_by_tile");
		map<uint64_t, unsigned> tile_to_gbt;

		for(unsigned bix = 0; bix < buildings.size(); ++bix) {
			unsigned gix;
			cube_t const &bcube(buildings[bix].bcube);
			uint64_t const tile_id(get_tile_id_containing_point_no_xyoff(bcube.get_cube_center()));
			auto it(tile_to_gbt.find(tile_id));

			if (it == tile_to_gbt.end()) { // new element
				gix = grid_by_tile.size();
				grid_by_tile.push_back(grid_elem_t());
				tile_to_gbt[tile_id] = gix;
			}
			else { // existing element
				gix = it->second;
				assert(gix < grid_by_tile.size());
			}
			grid_by_tile[gix].add(bcube, bix);
		} // for bix
	}

	bool check_valid_building_placement(building_params_t const &params, building_t const &b, vect_cube_t const &avoid_bcubes, cube_t const &avoid_bcubes_bcube,
		float min_building_spacing, unsigned plot_ix, bool non_city_only, bool use_city_plots, bool check_plot_coll)
	{
		float const expand_val(b.is_rotated() ? 0.05 : 0.1); // expand by 5-10% (relative - multiplied by building size)
		vector3d const b_sz(b.bcube.get_size());
		vector3d expand(expand_val*b_sz);
		for (unsigned d = 0; d < 2; ++d) {max_eq(expand[d], min_building_spacing);} // ensure the min building spacing (only applies to the current building)
		cube_t test_bc(b.bcube);
		test_bc.expand_by_xy(expand);

		if (use_city_plots) {
			assert(plot_ix < bix_by_plot.size());
			if (check_for_overlaps(bix_by_plot[plot_ix], test_bc, b, expand_val, min_building_spacing, points)) return 0;
			bix_by_plot[plot_ix].push_back(buildings.size());
		}
		else if (check_plot_coll && !avoid_bcubes.empty() && avoid_bcubes_bcube.intersects_xy(test_bc) &&
			has_bcube_int_xy(test_bc, avoid_bcubes, params.sec_extra_spacing)) // extra expand val
		{
			return 0;
		}
		else {
			float const extra_spacing(non_city_only ? params.sec_extra_spacing : 0.0); // absolute value of expand
			test_bc.expand_by_xy(extra_spacing);
			unsigned ixr[2][2];
			get_grid_range(test_bc, ixr);

			for (unsigned y = ixr[0][1]; y <= ixr[1][1]; ++y) {
				for (unsigned x = ixr[0][0]; x <= ixr[1][0]; ++x) {
					grid_elem_t const &ge(get_grid_elem(x, y));
					if (!test_bc.intersects_xy(ge.bcube)) continue;
					if (check_for_overlaps(ge.bc_ixs, test_bc, b, expand_val, max(min_building_spacing, extra_spacing), points)) {return 0;}
				} // for x
			} // for y
		}
		return 1;
	}

	struct building_cand_t : public building_t {
		vect_cube_t &temp_parts;
		building_cand_t(vect_cube_t &temp_parts_) : temp_parts(temp_parts_) {temp_parts.clear(); parts.swap(temp_parts);} // parts takes temp_parts memory
		~building_cand_t() {parts.swap(temp_parts);} // memory returned from parts to temp_parts
	};

public:
	building_creator_t() : grid_sz(1), gpu_mem_usage(0), max_extent(zero_vector), use_smap_this_frame(0) {}
	bool empty() const {return buildings.empty();}
	void clear() {buildings.clear(); grid.clear(); clear_vbos(); buildings_bcube = cube_t();}
	unsigned get_num_buildings() const {return buildings.size();}
	unsigned get_gpu_mem_usage() const {return gpu_mem_usage;}
	vector3d const &get_max_extent() const {return max_extent;}
	building_t const &get_building(unsigned ix) const {assert(ix < buildings.size()); return buildings[ix];}
	cube_t const &get_building_bcube(unsigned ix) const {return get_building(ix).bcube;}
	cube_t const &get_bcube() const {return buildings_bcube;}
	bool is_visible(vector3d const &xlate) const {return (!empty() && camera_pdu.cube_visible(buildings_bcube + xlate));}
	bool is_single_tile() const {return (grid_by_tile.size() == 1);}
	
	bool get_building_hit_color(point const &p1, point const &p2, colorRGBA &color) const { // exterior only
		float t(1.0); // unused
		unsigned hit_bix(0);
		unsigned const ret(check_line_coll(p1, p2, t, hit_bix, 0, 1)); // no_coll_pt=1; returns: 0=no hit, 1=hit side, 2=hit roof
		if (ret == 0) return 0;
		building_t const &b(get_building(hit_bix));
		switch (ret) {
		case 1: color = b.get_avg_side_color  (); break;
		case 2: color = b.get_avg_roof_color  (); break;
		case 3: color = b.get_avg_detail_color(); break;
		default: assert(0);
		}
		return 1;
	}
	void gen(building_params_t const &params, bool city_only, bool non_city_only, bool is_tile, int rseed=123) {
		assert(!(city_only && non_city_only));
		clear();
		if (params.tt_only && world_mode != WMODE_INF_TERRAIN) return;
		if (params.gen_inf_buildings() && !is_tile) return; // not added here
		vector<unsigned> const &mat_ix_list(params.get_mat_list(city_only, non_city_only));
		if (params.materials.empty() || mat_ix_list.empty()) return; // no materials
		timer_t timer("Gen Buildings", !is_tile);
		float const def_water_level(get_water_z_height()), min_building_spacing(get_min_obj_spacing());
		vector3d const offset(-xoff2*DX_VAL, -yoff2*DY_VAL, 0.0);
		vector3d const xlate((world_mode == WMODE_INF_TERRAIN) ? offset : zero_vector); // cancel out xoff2/yoff2 translate
		vector3d const delta_range((world_mode == WMODE_INF_TERRAIN) ? zero_vector : offset);
		range = params.materials[mat_ix_list.front()].pos_range; // range is union over all material ranges
		for (auto i = mat_ix_list.begin()+1; i != mat_ix_list.end(); ++i) {range.union_with_cube(params.materials[*i].pos_range);}
		range     += delta_range;
		range_sz   = range.get_size(); // Note: place_radius is relative to range cube center
		max_extent = zero_vector;
		assert(range_sz.x > 0.0 && range_sz.y > 0.0);
		UNROLL_2X(range_sz_inv[i_] = 1.0/range_sz[i_];) // xy only
		if (!is_tile) {buildings.reserve(params.num_place);}
		grid_sz = (is_tile ? 4 : 32); // tiles are small enough that they don't need grids
		grid.resize(grid_sz*grid_sz); // square
		unsigned num_tries(0), num_gen(0), num_skip(0);
		if (rseed == 0) {rseed = 123;} // 0 is a bad value
		rgen.set_state(rand_gen_index, rseed); // update when mesh changes, otherwise determinstic
		vect_cube_with_zval_t city_plot_bcubes;
		vect_cube_t avoid_bcubes;
		cube_t avoid_bcubes_bcube;
		if (city_only) {get_city_plot_bcubes(city_plot_bcubes);} // Note: assumes approx equal area for placement distribution
		
		if (non_city_only) {
			get_city_bcubes(avoid_bcubes);
			get_city_road_bcubes(avoid_bcubes, 1); // connector roads only
			get_all_model_bcubes(avoid_bcubes);
			expand_cubes_by_xy(avoid_bcubes, get_road_max_width());
			for (auto i = avoid_bcubes.begin(); i != avoid_bcubes.end(); ++i) {avoid_bcubes_bcube.assign_or_union_with_cube(*i);}
		}
		bool const use_city_plots(!city_plot_bcubes.empty()), check_plot_coll(!avoid_bcubes.empty());
		bix_by_plot.resize(city_plot_bcubes.size());
		point center(all_zeros);
		unsigned num_consec_fail(0), max_consec_fail(0);
		vect_cube_t temp_parts;

		for (unsigned i = 0; i < params.num_place; ++i) {
			bool success(0);

			for (unsigned n = 0; n < params.num_tries; ++n) { // 10 tries to find a non-overlapping building placement
				building_cand_t b(temp_parts);
				b.mat_ix = params.choose_rand_mat(rgen, city_only, non_city_only); // set material
				building_mat_t const &mat(b.get_material());
				cube_t pos_range;
				unsigned plot_ix(0);
				
				if (use_city_plots) { // select a random plot, if available
					plot_ix   = rgen.rand()%city_plot_bcubes.size();
					pos_range = city_plot_bcubes[plot_ix];
					center.z  = city_plot_bcubes[plot_ix].zval; // optimization: take zval from plot rather than calling get_exact_zval()
					pos_range.expand_by_xy(-min_building_spacing); // force min spacing between building and edge of plot
				}
				else {
					pos_range = mat.pos_range + delta_range;
				}
				vector3d const pos_range_sz(pos_range.get_size());
				assert(pos_range_sz.x > 0.0 && pos_range_sz.y > 0.0);
				point const place_center(pos_range.get_cube_center());
				bool keep(0);
				++num_tries;

				for (unsigned m = 0; m < params.num_tries; ++m) {
					for (unsigned d = 0; d < 2; ++d) {center[d] = rgen.rand_uniform(pos_range.d[d][0], pos_range.d[d][1]);} // x,y
					if (is_tile || mat.place_radius == 0.0 || dist_xy_less_than(center, place_center, mat.place_radius)) {keep = 1; break;} // place_radius ignored for tiles
				}
				if (!keep) continue; // placement failed, skip
				b.is_house = (mat.house_prob > 0.0 && rgen.rand_float() < mat.house_prob);
				float const size_scale(b.is_house ? mat.gen_size_scale(rgen) : 1.0);
				
				for (unsigned d = 0; d < 2; ++d) { // x,y
					float const sz(0.5*size_scale*rgen.rand_uniform(min(mat.sz_range.d[d][0], 0.3f*pos_range_sz[d]),
						                                            min(mat.sz_range.d[d][1], 0.5f*pos_range_sz[d]))); // use pos range size for max
					b.bcube.d[d][0] = center[d] - sz;
					b.bcube.d[d][1] = center[d] + sz;
				}
				if ((use_city_plots || is_tile) && !pos_range.contains_cube_xy(b.bcube)) continue; // not completely contained in plot/tile (pre-rot)
				if (!use_city_plots) {b.gen_rotation(rgen);} // city plots are Manhattan (non-rotated) - must rotate before bcube checks below
				if (is_tile && !pos_range.contains_cube_xy(b.bcube)) continue; // not completely contained in tile
				if (start_in_inf_terrain && b.bcube.contains_pt_xy(get_camera_pos())) continue; // don't place a building over the player appearance spot
				if (!check_valid_building_placement(params, b, avoid_bcubes, avoid_bcubes_bcube,
					min_building_spacing, plot_ix, non_city_only, use_city_plots, check_plot_coll)) continue; // check overlap
				++num_gen;
				if (!use_city_plots) {center.z = get_exact_zval(center.x+xlate.x, center.y+xlate.y);} // only calculate when needed
				float const z_sea_level(center.z - def_water_level);
				if (z_sea_level < 0.0) break; // skip underwater buildings, failed placement
				if (z_sea_level < mat.min_alt || z_sea_level > mat.max_alt) break; // skip bad altitude buildings, failed placement
				float const hmin(use_city_plots ? pos_range.z1() : 0.0), hmax(use_city_plots ? pos_range.z2() : 1.0);
				assert(hmin <= hmax);
				float const height_range(mat.sz_range.dz());
				assert(height_range >= 0.0);
				float const z_size_scale(size_scale*(b.is_house ? rgen.rand_uniform(0.6, 0.8) : 1.0)); // make houses slightly shorter on average to offset extra height added by roof
				float const height_val(z_size_scale*(mat.sz_range.z1() + height_range*rgen.rand_uniform(hmin, hmax)));
				assert(height_val > 0.0);
				b.set_z_range(center.z, (center.z + 0.5*height_val));
				assert(b.bcube.is_strictly_normalized());
				mat.side_color.gen_color(b.side_color, rgen);
				mat.roof_color.gen_color(b.roof_color, rgen);
				add_to_grid(b.bcube, buildings.size());
				vector3d const sz(b.bcube.get_size());
				float const mult[3] = {0.5, 0.5, 1.0}; // half in X,Y and full in Z
				UNROLL_3X(max_extent[i_] = max(max_extent[i_], mult[i_]*sz[i_]);)
				buildings.push_back(b);
				success = 1;
				break; // done
			} // for n
			if (success) {num_consec_fail = 0;}
			else {
				++num_consec_fail;
				max_eq(max_consec_fail, num_consec_fail);

				if (num_consec_fail >= (is_tile ? 50U : 5000U)) { // too many failures - give up
					if (!is_tile) {cout << "Failed to place a building after " << num_consec_fail << " tries, giving up after " << i << " iterations" << endl;}
					break;
				}
			}
		} // for i
		if (buildings.capacity() > 2*buildings.size()) {buildings.shrink_to_fit();}
		bix_by_x1 cmp_x1(buildings);
		for (auto i = bix_by_plot.begin(); i != bix_by_plot.end(); ++i) {sort(i->begin(), i->end(), cmp_x1);}
		if (!is_tile) {timer.end();} // use a single timer for tile mode

		if (params.flatten_mesh && !use_city_plots) { // not needed for city plots, which are already flat
			timer_t timer("Gen Building Zvals", !is_tile);
			bool const do_flatten(using_tiled_terrain_hmap_tex());

#pragma omp parallel for schedule(static,1) if (!is_tile)
			for (int i = 0; i < (int)buildings.size(); ++i) {
				building_t &b(buildings[i]);

				if (do_flatten) {
					//assert(!b.is_rotated()); // too strong?
					flatten_hmap_region(b.bcube); // flatten the mesh under the bcube to a height of mesh_zval
				}
				else { // extend building bottom downward to min mesh height
					float &zmin(b.bcube.d[2][0]); // Note: grid bcube z0 value won't be correct, but will be fixed conservatively below
					float const zmin0(zmin);
					unsigned num_below(0);
					
					for (int d = 0; d < 4; ++d) {
						float const zval(get_exact_zval(b.bcube.d[0][d&1]+xlate.x, b.bcube.d[1][d>>1]+xlate.y)); // approximate for rotated buildings
						min_eq(zmin, zval);
						num_below += (zval < def_water_level);
					}
					max_eq(zmin, def_water_level); // don't go below the water
					float const max_dz(b.get_material().max_delta_z);
					if (num_below > 2 || // more than 2 corners underwater
						(max_dz > 0.0 && (zmin0 - zmin) > max_dz)) // too steep of a slope
					{
						b.bcube.set_to_zeros();
						++num_skip;
					}
					else if (!b.parts.empty()) {
						b.parts.back().d[2][0] = b.bcube.d[2][0]; // update base z1
						assert(b.parts.back().dz() > 0.0);
					}
				}
			} // for i
			if (do_flatten) { // use conservative zmin for grid
				for (auto i = grid.begin(); i != grid.end(); ++i) {i->bcube.d[2][0] = def_water_level;}
			}
		} // if flatten_mesh
		{ // open a scope
			timer_t timer2("Gen Building Geometry", !is_tile);
#pragma omp parallel for schedule(static,1) if (!is_tile)
			for (int i = 0; i < (int)buildings.size(); ++i) {buildings[i].gen_geometry(i, 1337*i+rseed);}
		} // close the scope
		for (auto g = grid.begin(); g != grid.end(); ++g) { // update grid bcube zvals to include building roofs
			for (auto b = g->bc_ixs.begin(); b != g->bc_ixs.end(); ++b) {
				cube_t &bbc(*b);
				bbc = get_building(b->ix).bcube;
				buildings_bcube.assign_or_union_with_cube(bbc);
				g->bcube.union_with_cube(bbc);
			}
		}
		if (!is_tile) {
			cout << "WM: " << world_mode << " MCF: " << max_consec_fail << " Buildings: " << params.num_place << " / " << num_tries << " / " << num_gen
				 << " / " << buildings.size() << " / " << (buildings.size() - num_skip) << endl;
		}
		build_grid_by_tile(is_tile);
		create_vbos(is_tile);
	}

	static void multi_draw_shadow(vector3d const &xlate, vector<building_creator_t *> const &bcs) {
		//timer_t timer("Draw Buildings Shadow");
		fgPushMatrix();
		translate_to(xlate);
		shader_t s;
		s.begin_color_only_shader(); // really don't even need colors
		for (auto i = bcs.begin(); i != bcs.end(); ++i) {(*i)->building_draw_vbo.draw(1);}
		s.end_shader();
		fgPopMatrix();
	}
	static void multi_draw_no_shadows(vector<building_creator_t *> const &bcs, unsigned max_draw_ix) {
		for (unsigned ix = 0; ix < max_draw_ix; ++ix) {
			for (auto i = bcs.begin(); i != bcs.end(); ++i) {
				if (!(*i)->use_smap_this_frame) {(*i)->building_draw_vbo.draw_block(ix, 0);} // non-smap pass, can skip tiles that will be drawn below
			}
		}
	}
	static bool check_tile_smap(bool shadow_only) {
		return (!shadow_only && world_mode == WMODE_INF_TERRAIN && shadow_map_enabled());
	}
	static void multi_draw(int shadow_only, vector3d const &xlate, vector<building_creator_t *> const &bcs) {
		if (bcs.empty()) return;
		if (shadow_only) {multi_draw_shadow(xlate, bcs); return;}
		//timer_t timer("Draw Buildings"); // 0.57ms (2.6ms with glFinish())
		point const camera(get_camera_pos()), camera_xlated(camera - xlate);
		int const use_bmap(global_building_params.has_normal_map);
		bool const use_tt_smap(check_tile_smap(0) && (light_valid_and_enabled(0) || light_valid_and_enabled(1))); // check for sun or moon
		bool const night(is_night(WIND_LIGHT_ON_RAND));
		bool have_windows(0), have_wind_lights(0), have_interior(0);
		unsigned max_draw_ix(0);
		shader_t s;

		for (auto i = bcs.begin(); i != bcs.end(); ++i) {
			assert(*i);
			have_windows     |= !(*i)->building_draw_windows.empty();
			have_wind_lights |= !(*i)->building_draw_wind_lights.empty();
			have_interior    |= (draw_building_interiors && !(*i)->building_draw_interior.empty());
			max_eq(max_draw_ix, (*i)->building_draw_vbo.get_num_draw_blocks());
			if (night) {(*i)->ensure_window_lights_vbos();}
			
			if ((*i)->is_single_tile()) { // only for tiled buildings
				(*i)->use_smap_this_frame = (use_tt_smap && try_bind_tile_smap_at_point(((*i)->grid_by_tile[0].bcube.get_cube_center() + xlate), s, 1)); // check_only=1
			}
		}
		bool const transparent_windows(DRAW_WINDOWS_AS_HOLES && have_windows && draw_building_interiors); // reuse draw_building_interiors for now
		bool const v(world_mode == WMODE_GROUND), indir(v), dlights(v), use_smap(v);
		float const min_alpha = 0.0; // 0.0 to avoid alpha test
		fgPushMatrix();
		translate_to(xlate);

		// draw building interiors with standard shader and no shadow maps; must be drawn first before windows depth pass
		if (have_interior) {
			//timer_t timer2("Draw Building Interiors");
			// TODO_INT: add room lights?
			setup_smoke_shaders(s, min_alpha, 0, 0, indir, 1, dlights, 0, 0, 0, use_bmap);
			s.add_uniform_float("diffuse_scale", 0.0); // disable diffuse and specular lighting for sun/moon
			s.add_uniform_float("ambient_scale", 1.5); // brighter ambient

			if (global_building_params.wall_split_thresh >= 4.0) { // draw all interiors (less CPU time, more GPU time, no draw artifacts)
				for (unsigned ix = 0; ix < max_draw_ix; ++ix) {
					for (auto i = bcs.begin(); i != bcs.end(); ++i) {(*i)->building_draw_interior.draw_block(ix, 0);}
				}
			}
			else { // draw only nearby interiors (less GPU time, more CPU time, much faster for dense walls)
				float const interior_draw_dist(2.0f*(X_SCENE_SIZE + Y_SCENE_SIZE));

				for (auto i = bcs.begin(); i != bcs.end(); ++i) {
					for (auto g = (*i)->grid_by_tile.begin(); g != (*i)->grid_by_tile.end(); ++g) { // Note: all grids should be nonempty
						if (!g->bcube.closest_dist_less_than(camera_xlated, interior_draw_dist)) continue; // too far
						point const pos(g->bcube.get_cube_center() + xlate);
						if (!camera_pdu.sphere_and_cube_visible_test(pos, g->bcube.get_bsphere_radius(), (g->bcube + xlate))) continue; // VFC
						(*i)->building_draw_interior.draw_tile(g - (*i)->grid_by_tile.begin());
					} // for g
				} // for i
			}
			s.add_uniform_float("diffuse_scale", 1.0); // re-enable diffuse and specular lighting for sun/moon
			s.add_uniform_float("ambient_scale", 1.0); // reset to default
			s.end_shader();
		}
		// main/batched draw pass
		setup_smoke_shaders(s, min_alpha, 0, 0, indir, 1, dlights, 0, 0, (use_smap ? 2 : 1), use_bmap, 0, 0, 0, 0.0, 0.0, 0, 0, 1); // is_outside=1
		for (auto i = bcs.begin(); i != bcs.end(); ++i) {(*i)->building_draw.init_draw_frame();}

		if (transparent_windows) {
			// draw back faces of buildings
			glEnable(GL_CULL_FACE);
			glCullFace(GL_FRONT);
			s.add_uniform_float("diffuse_scale", 0.0); // disable diffuse and specular lighting for sun/moon
			multi_draw_no_shadows(bcs, max_draw_ix);
			s.add_uniform_float("diffuse_scale", 1.0); // reset
			s.disable();
			glCullFace(GL_BACK); // draw front faces
			// draw windows in depth pass to create holes
			// TODO_INT: figure out how to draw window holes on back faces so that the player can look completely through buildings
			shader_t holes_shader;
			setup_smoke_shaders(holes_shader, 0.9, 0, 0, 0, 0, 0, 0); // min_alpha=0.9 for depth test - need same shader to avoid z-fighting
			glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // Disable color writing, we only want to write to the Z-Buffer
			for (auto i = bcs.begin(); i != bcs.end(); ++i) {(*i)->building_draw_windows.draw(0);} // draw windows on top of other buildings
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			holes_shader.end_shader();
			// draw front faces of buildings
			s.enable();
			multi_draw_no_shadows(bcs, max_draw_ix);
			glDisable(GL_CULL_FACE);
		}
		else {
			multi_draw_no_shadows(bcs, max_draw_ix); // normal drawing of all buildings
		}
		glDepthFunc(GL_LEQUAL);

		if (have_windows) { // draw windows
			enable_blend();
			glDepthMask(GL_FALSE); // disable depth writing

			for (auto i = bcs.begin(); i != bcs.end(); ++i) { // draw windows on top of other buildings
				// need to swap opaque window texture with transparent texture for this draw pass
				if (transparent_windows) {(*i)->building_draw_windows.toggle_transparent_windows_mode();}
				(*i)->building_draw_windows.draw(0);
				if (transparent_windows) {(*i)->building_draw_windows.toggle_transparent_windows_mode();}
			}
			glDepthMask(GL_TRUE); // re-enable depth writing
			disable_blend();
		}
		s.end_shader();

		// post-pass to render buildings in nearby tiles that have shadow maps
		if (use_tt_smap) {
			//timer_t timer2("Draw Buildings Smap"); // 0.3
			city_shader_setup(s, 1, 1, use_bmap, min_alpha); // use_smap=1, use_dlights=1
			float const draw_dist(get_tile_smap_dist() + 0.5f*(X_SCENE_SIZE + Y_SCENE_SIZE));
			glEnable(GL_CULL_FACE); // cull back faces to avoid lighting/shadows on inside walls of building interiors

			for (auto i = bcs.begin(); i != bcs.end(); ++i) {
				bool const no_depth_write(!(*i)->is_single_tile());
				if (no_depth_write) {glDepthMask(GL_FALSE);} // disable depth writing

				for (auto g = (*i)->grid_by_tile.begin(); g != (*i)->grid_by_tile.end(); ++g) { // Note: all grids should be nonempty
					if (!g->bcube.closest_dist_less_than(camera_xlated, draw_dist)) continue; // too far
					point const pos(g->bcube.get_cube_center() + xlate);
					if (!camera_pdu.sphere_and_cube_visible_test(pos, g->bcube.get_bsphere_radius(), (g->bcube + xlate))) continue; // VFC
					if (!try_bind_tile_smap_at_point(pos, s)) continue; // no shadow maps - not drawn in this pass
					unsigned const tile_id(g - (*i)->grid_by_tile.begin());
					(*i)->building_draw_vbo.draw_tile(tile_id);

					if (!(*i)->building_draw_windows.empty()) {
						enable_blend();
						if (!no_depth_write) {glDepthMask(GL_FALSE);} // always disable depth writing
						if (transparent_windows) {(*i)->building_draw_windows.toggle_transparent_windows_mode();}
						(*i)->building_draw_windows.draw_tile(tile_id); // draw windows on top of other buildings
						if (transparent_windows) {(*i)->building_draw_windows.toggle_transparent_windows_mode();}
						if (!no_depth_write) {glDepthMask(GL_TRUE);} // always re-enable depth writing
						disable_blend();
					}
				} // for g
				if (no_depth_write) {glDepthMask(GL_TRUE);} // re-enable depth writing
			} // for i
			glDisable(GL_CULL_FACE);
			s.end_shader();
		}
		if (night && have_wind_lights) { // add night time random lights in windows
			enable_blend();
			glDepthMask(GL_FALSE); // disable depth writing
			float const low_v(0.5 - WIND_LIGHT_ON_RAND), high_v(0.5 + WIND_LIGHT_ON_RAND), lit_thresh_mult(1.0 + 2.0*CLIP_TO_01((light_factor - low_v)/(high_v - low_v)));
			s.end_shader();
			s.set_vert_shader("window_lights");
			s.set_frag_shader("linear_fog.part+window_lights");
			s.set_prefix("#define FOG_FADE_TO_TRANSPARENT", 1);
			setup_tt_fog_pre(s);
			s.begin_shader();
			s.add_uniform_float("lit_thresh_mult", lit_thresh_mult); // gradual transition of lit window probability around sunset
			setup_tt_fog_post(s);
			for (auto i = bcs.begin(); i != bcs.end(); ++i) {(*i)->building_draw_wind_lights.draw(0);} // add bloom?
			glDepthMask(GL_TRUE); // re-enable depth writing
			disable_blend();
		}
		glDepthFunc(GL_LESS);
		fgPopMatrix();
	}
	void draw_building_lights(vector3d const &xlate) { // add night time lights to buildings; non-const because it modifies building_lights
		if (empty() || !is_night(WIND_LIGHT_ON_RAND)) return;
		//timer_t timer("Building Lights"); // 0.06ms
		set_additive_blend_mode();
		enable_blend();
		glDepthMask(GL_FALSE); // disable depth writing
		vector3d const max_extent(get_buildings_max_extent());
		float const draw_dist(20.0*max_extent.mag());
		point const camera(get_camera_pos() - xlate); // in building space
		colorRGBA const light_colors[16] = {RED,RED,RED,RED,RED,RED,RED,RED, BLUE,BLUE,BLUE,BLUE, WHITE,WHITE, YELLOW, GREEN};

		for (auto g = grid_by_tile.begin(); g != grid_by_tile.end(); ++g) {
			if (!g->bcube.closest_dist_less_than(camera, draw_dist)) continue; // too far away
			if (!camera_pdu.cube_visible(g->bcube + xlate)) continue;

			for (auto i = g->bc_ixs.begin(); i != g->bc_ixs.end(); ++i) {
				building_t const &b(get_building(i->ix));
				if (!b.has_antenna) continue;
				if (!is_night((((321*i->ix) & 7)/7.0)*WIND_LIGHT_ON_RAND)) continue; // gradually turn on
				if (!b.bcube.closest_dist_less_than(camera, draw_dist)) continue; // too far away
				if (!camera_pdu.cube_visible(b.bcube + xlate)) continue;
				cube_t const &antenna(b.details.back());
				unsigned const num_segs(max(1U, (((123*i->ix) & 3) + unsigned(6.0*antenna.dz()/max_extent.z)))); // some mix of height and randomness
				point const center(antenna.get_cube_center());
				point pos(point(center.x, center.y, antenna.z2()) + xlate);
				float const radius(1.2f*(antenna.dx() + antenna.dy())), z_step(0.6*antenna.dz()/num_segs);
				float const alpha(min(1.0f, 1.5f*(1.0f - p2p_dist(camera, center)/draw_dist))); // fade with distance
				colorRGBA const color(light_colors[i->ix & 15], alpha);

				for (unsigned n = 0; n < num_segs; ++n) { // distribute lights along top half of antenna
					building_lights.add_pt(sized_vert_t<vert_color>(vert_color(pos, color), radius));
					pos.z -= z_step;
				}
			} // for i
		} // for g
		building_lights.draw_and_clear(BLUR_TEX, 0.0, 0, 1, 0.005); // use geometry shader for unlimited point size
		glDepthMask(GL_TRUE); // re-enable depth writing
		disable_blend();
		set_std_blend_mode();
	}

	void get_all_window_verts(building_draw_t &bdraw, bool light_pass) {
		bdraw.clear();

		for (auto g = grid_by_tile.begin(); g != grid_by_tile.end(); ++g) { // Note: all grids should be nonempty
			bdraw.cur_tile_id = (g - grid_by_tile.begin());
			for (auto i = g->bc_ixs.begin(); i != g->bc_ixs.end(); ++i) {get_building(i->ix).get_all_drawn_window_verts(bdraw, light_pass);}
		}
		bdraw.finalize(grid_by_tile.size());
	}
	void get_all_drawn_verts() { // Note: non-const; building_draw is modified
		if (buildings.empty()) return;
		//timer_t timer("Get Building Verts"); // 39/115
#pragma omp parallel for schedule(static) num_threads(3)
		for (int pass = 0; pass < 3; ++pass) { // parallel loop doesn't help much because pass 0 takes most of the time
			if (pass == 0) { // exterior pass
				building_draw_vbo.clear();

				for (auto g = grid_by_tile.begin(); g != grid_by_tile.end(); ++g) { // Note: all grids should be nonempty
					building_draw_vbo.cur_tile_id = (g - grid_by_tile.begin());
					for (auto i = g->bc_ixs.begin(); i != g->bc_ixs.end(); ++i) {get_building(i->ix).get_all_drawn_verts(building_draw_vbo, 1, 0);}
				}
				building_draw_vbo.finalize(grid_by_tile.size());
			}
			else if (pass == 1) { // interior pass
				// pre-allocate interior wall, celing, and floor verts, assuming all buildings have the same materials
				unsigned num_floors(0), num_ceils(0), num_walls(0);
				building_mat_t const &mat(buildings.front().get_material());
				
				for (auto b = buildings.begin(); b != buildings.end(); ++b) {
					if (!b->interior) continue; // no interior
					building_mat_t const &mat2(b->get_material());
					if (mat2.floor_tex == mat.floor_tex) {num_floors += b->interior->floors.size();}
					if (mat2.ceil_tex  == mat.ceil_tex ) {num_ceils  += b->interior->ceilings.size();}
					if (mat2.wall_tex  == mat.wall_tex ) {num_walls  += b->interior->walls[0].size() + b->interior->walls[1].size();}
				}
				building_draw_interior.reserve_verts(mat.floor_tex, 4*num_floors); // top surface only
				building_draw_interior.reserve_verts(mat.ceil_tex,  4*num_ceils ); // bottom surface only
				building_draw_interior.reserve_verts(mat.wall_tex, 16*num_walls ); // X/Y surfaces (4x)
				// generate vertex data
				building_draw_interior.clear();

				for (auto g = grid_by_tile.begin(); g != grid_by_tile.end(); ++g) { // Note: all grids should be nonempty
					building_draw_interior.cur_tile_id = (g - grid_by_tile.begin());
					for (auto i = g->bc_ixs.begin(); i != g->bc_ixs.end(); ++i) {get_building(i->ix).get_all_drawn_verts(building_draw_interior, 0, 1);}
				}
				building_draw_interior.finalize(grid_by_tile.size());
			}
			else if (pass == 2) { // windows pass
				get_all_window_verts(building_draw_windows, 0);
				if (is_night(WIND_LIGHT_ON_RAND)) {get_all_window_verts(building_draw_wind_lights, 1);} // only generate window verts at night
			}
		} // for pass
	}
	void create_vbos(bool is_tile) { // Note: non-const; building_draw is modified
		building_window_gen.check_windows_texture();
		tid_mapper.init();
		timer_t timer("Create Building VBOs", !is_tile);
		get_all_drawn_verts();
		
		if (!is_tile) {
			unsigned const num_everts(building_draw_vbo.num_verts()), num_etris(building_draw_vbo.num_tris());
			unsigned const num_iverts(building_draw_interior.num_verts()), num_itris(building_draw_interior.num_tris());
			gpu_mem_usage = (num_everts + num_iverts)*sizeof(vert_norm_comp_tc_color);
			cout << "Building V: " << num_everts << ", T: " << num_etris << ", interior V: " << num_iverts << ", T: " << num_itris << ", mem: " << gpu_mem_usage << endl;
		}
		building_draw_vbo.upload_to_vbos();
		building_draw_windows.upload_to_vbos();
		building_draw_wind_lights.upload_to_vbos(); // Note: may be empty if not night time
		building_draw_interior.upload_to_vbos();
	}
	void ensure_window_lights_vbos() {
		if (!building_draw_wind_lights.empty()) return; // already calculated
		building_window_gen.check_windows_texture();
		get_all_window_verts(building_draw_wind_lights, 1);
		building_draw_wind_lights.upload_to_vbos();
	}
	void clear_vbos() {
		building_draw.clear_vbos();
		building_draw_vbo.clear_vbos();
		building_draw_windows.clear_vbos();
		building_draw_wind_lights.clear_vbos();
		building_draw_interior.clear_vbos();
	}

	bool check_sphere_coll(point &pos, point const &p_last, float radius, bool xy_only=0, vector3d *cnorm=nullptr, bool check_interior=0) const {
		if (empty()) return 0;
		vector3d const xlate(get_camera_coord_space_xlate());

		if (radius == 0.0) { // point coll - ignore p_last as well
			point const p1x(pos - xlate);
			unsigned const gix(get_grid_ix(p1x));
			grid_elem_t const &ge(grid[gix]);
			if (ge.bc_ixs.empty()) return 0; // skip empty grid
			if (!(xy_only ? ge.bcube.contains_pt_xy(p1x) : ge.bcube.contains_pt(p1x))) return 0; // no intersection - skip this grid
			vector<point> points; // reused across calls

			for (auto b = ge.bc_ixs.begin(); b != ge.bc_ixs.end(); ++b) {
				if (!(xy_only ? b->contains_pt_xy(p1x) : b->contains_pt(p1x))) continue;
				if (get_building(b->ix).check_sphere_coll(pos, p_last, xlate, 0.0, xy_only, points, cnorm, check_interior)) return 1;
			}
			return 0; // no coll
		}
		cube_t bcube; bcube.set_from_sphere((pos - xlate), radius);
		unsigned ixr[2][2];
		get_grid_range(bcube, ixr);
		float const dist(p2p_dist(pos, p_last));
		vector<point> points; // reused across calls

		for (unsigned y = ixr[0][1]; y <= ixr[1][1]; ++y) {
			for (unsigned x = ixr[0][0]; x <= ixr[1][0]; ++x) {
				grid_elem_t const &ge(get_grid_elem(x, y));
				if (ge.bc_ixs.empty()) continue; // skip empty grid
				if (!(xy_only ? sphere_cube_intersect_xy(pos, (radius + dist), (ge.bcube + xlate)) :
					sphere_cube_intersect(pos, (radius + dist), (ge.bcube + xlate)))) continue; // Note: makes little difference

				// Note: assumes buildings are separated so that only one sphere collision can occur
				for (auto b = ge.bc_ixs.begin(); b != ge.bc_ixs.end(); ++b) {
					if (!b->intersects_xy(bcube)) continue;
					if (get_building(b->ix).check_sphere_coll(pos, p_last, xlate, radius, xy_only, points, cnorm, check_interior)) return 1;
				}
			} // for x
		} // for y
		return 0;
	}

	unsigned check_line_coll(point const &p1, point const &p2, float &t, unsigned &hit_bix, bool ret_any_pt, bool no_coll_pt) const {
		if (empty()) return 0;
		vector3d const xlate(get_camera_coord_space_xlate());
		point const p1x(p1 - xlate);
		vector<point> points; // reused across calls

		if (p1.x == p2.x && p1.y == p2.y) { // vertical line special case optimization (for example map mode)
			if (!get_bcube().contains_pt_xy(p1x)) return 0;
			unsigned const gix(get_grid_ix(p1x));
			grid_elem_t const &ge(grid[gix]);
			if (ge.bc_ixs.empty()) return 0; // skip empty grid
			if (!ge.bcube.contains_pt_xy(p1x)) return 0; // no intersection - skip this grid

			for (auto b = ge.bc_ixs.begin(); b != ge.bc_ixs.end(); ++b) {
				if (!b->contains_pt_xy(p1x)) continue;
				unsigned const ret(get_building(b->ix).check_line_coll(p1, p2, xlate, t, points, 0, ret_any_pt, no_coll_pt));
				if (ret) {hit_bix = b->ix; return ret;} // can only intersect one building
			} // for b
			return 0; // no coll
		}
		cube_t bcube(p1x, p2-xlate);
		unsigned ixr[2][2];
		get_grid_range(bcube, ixr);
		unsigned coll(0); // 0=none, 1=side, 2=roof
		point end_pos(p2);

		// for now, just do a slow iteration over every grid element within the line's bbox in XY
		// Note: should probably iterate over the grid in XY order from the start to the end of the line, or better yet use a line drawing algorithm
		for (unsigned y = ixr[0][1]; y <= ixr[1][1]; ++y) {
			for (unsigned x = ixr[0][0]; x <= ixr[1][0]; ++x) {
				grid_elem_t const &ge(get_grid_elem(x, y));
				if (ge.bc_ixs.empty()) continue; // skip empty grid
				if (!check_line_clip(p1x, (end_pos - xlate), ge.bcube.d)) continue; // no intersection - skip this grid

				for (auto b = ge.bc_ixs.begin(); b != ge.bc_ixs.end(); ++b) { // Note: okay to check the same building more than once
					if (!b->intersects(bcube)) continue;
					float t_new(t);
					unsigned const ret(get_building(b->ix).check_line_coll(p1, p2, xlate, t_new, points, 0, ret_any_pt, no_coll_pt));

					if (ret && t_new <= t) { // closer hit pos, update state
						t = t_new; hit_bix = b->ix; coll = ret;
						end_pos = p1 + t*(p2 - p1);
						if (ret_any_pt) return coll;
					}
				} // for b
			} // for x
		} // for y
		return coll; // 0=none, 1=side, 2=roof, 3=details
	}

	// Note: we can get building_id by calling check_ped_coll() or get_building_bcube_at_pos()
	bool check_line_coll_building(point const &p1, point const &p2, unsigned building_id) const { // Note: not thread safe due to static points
		assert(building_id < buildings.size());
		static vector<point> points; // reused across calls
		float t_new(1.0);
		return buildings[building_id].check_line_coll(p1, p2, zero_vector, t_new, points, 0, 1);
	}

	int get_building_bcube_contains_pos(point const &pos) { // Note: not thread safe due to static points
		if (empty()) return -1;
		unsigned const gix(get_grid_ix(pos));
		grid_elem_t const &ge(grid[gix]);
		if (ge.bc_ixs.empty() || !ge.bcube.contains_pt(pos)) return -1; // skip empty or non-containing grid
		static vector<point> points; // reused across calls

		for (auto b = ge.bc_ixs.begin(); b != ge.bc_ixs.end(); ++b) {
			if (b->contains_pt(pos)) {return b->ix;} // found
		}
		return -1;
	}

	bool check_ped_coll(point const &pos, float radius, unsigned plot_id, unsigned &building_id) const { // Note: not thread safe due to static points
		if (empty()) return 0;
		assert(plot_id < bix_by_plot.size());
		vector<unsigned> const &bixes(bix_by_plot[plot_id]); // should be populated in gen()
		if (bixes.empty()) return 0;
		cube_t bcube; bcube.set_from_sphere(pos, radius);
		static vector<point> points; // reused across calls

		// Note: assumes buildings are separated so that only one ped collision can occur
		for (auto b = bixes.begin(); b != bixes.end(); ++b) {
			building_t const &building(get_building(*b));
			if (building.bcube.x1() > bcube.x2()) break; // no further buildings can intersect (sorted by x1)
			if (!building.bcube.intersects_xy(bcube)) continue;
			if (building.check_point_or_cylin_contained(pos, 2.0*radius, points)) {building_id = *b; return 1;} // double the radius value to add padding to account for inaccuracy
		}
		return 0;
	}
	bool select_building_in_plot(unsigned plot_id, unsigned rand_val, unsigned &building_id) const {
		if (bix_by_plot.empty()) return 0; // not setup / no buildings
		assert(plot_id < bix_by_plot.size());
		vector<unsigned> const &bixes(bix_by_plot[plot_id]);
		if (bixes.empty()) return 0;
		building_id = bixes[rand_val % bixes.size()];
		return 1;
	}

	void get_overlapping_bcubes(cube_t const &xy_range, vect_cube_t &bcubes) const { // Note: called on init, don't need to use get_camera_coord_space_xlate()
		if (empty()) return; // nothing to do
		unsigned ixr[2][2];
		get_grid_range(xy_range, ixr);

		for (unsigned y = ixr[0][1]; y <= ixr[1][1]; ++y) {
			for (unsigned x = ixr[0][0]; x <= ixr[1][0]; ++x) {
				grid_elem_t const &ge(get_grid_elem(x, y));
				if (ge.bc_ixs.empty() || !xy_range.intersects_xy(ge.bcube)) continue;

				for (auto b = ge.bc_ixs.begin(); b != ge.bc_ixs.end(); ++b) {
					if (!xy_range.intersects_xy(*b)) continue;
					cube_t shared(xy_range);
					shared.intersect_with_cube(*b);
					if (get_grid_ix(shared.get_llc()) == y*grid_sz + x) {bcubes.push_back(*b);} // add only if in home grid (to avoid duplicates)
				}
			} // for x
		} // for y
	}

	void get_occluders(pos_dir_up const &pdu, building_occlusion_state_t &state) const {
		state.init(pdu.pos, get_camera_coord_space_xlate());
		
		for (auto g = grid.begin(); g != grid.end(); ++g) {
			if (g->bc_ixs.empty()) continue;
			point const pos(g->bcube.get_cube_center() + state.xlate);
			if (!pdu.sphere_and_cube_visible_test(pos, g->bcube.get_bsphere_radius(), (g->bcube + state.xlate))) continue; // VFC
			
			for (auto b = g->bc_ixs.begin(); b != g->bc_ixs.end(); ++b) {
				if (pdu.cube_visible(*b + state.xlate)) {state.building_ids.push_back(b->ix);}
			}
		}
	}
	bool check_pts_occluded(point const *const pts, unsigned npts, building_occlusion_state_t &state) const {
		for (vector<unsigned>::const_iterator b = state.building_ids.begin(); b != state.building_ids.end(); ++b) {
			building_t const &building(get_building(*b));
			bool occluded(1);

			for (unsigned i = 0; i < npts; ++i) {
				float t(1.0); // start at end of line
				if (!building.check_line_coll(state.pos, pts[i], state.xlate, t, state.temp_points, 1)) {occluded = 0; break;}
			}
			if (occluded) return 1;
		} // for b
		return 0;
	}
}; // building_creator_t


class building_tiles_t {
	map<pair<int, int>, building_creator_t> tiles; // key is {x, y} pair
	vector3d max_extent;
public:
	building_tiles_t() : max_extent(zero_vector) {}
	bool     empty() const {return tiles.empty();}
	unsigned size()  const {return tiles.size();}
	vector3d get_max_extent() const {return max_extent;}

	bool create_tile(int x, int y) {
		auto it(tiles.find(make_pair(x, y)));
		if (it != tiles.end()) return 0; // already exists
		//cout << "Create building tile " << x << "," << y << ", tiles: " << tiles.size() << endl; // 299 tiles
		building_creator_t &bc(tiles[make_pair(x, y)]); // insert it
		assert(bc.empty());
		cube_t bcube(all_zeros);
		bcube.x1() = get_xval(x*MESH_X_SIZE);
		bcube.y1() = get_yval(y*MESH_Y_SIZE);
		bcube.x2() = get_xval((x+1)*MESH_X_SIZE);
		bcube.y2() = get_yval((y+1)*MESH_Y_SIZE);
		global_building_params.set_pos_range(bcube);
		int const rseed(x + (y << 16) + 12345); // should not be zero
		bc.gen(global_building_params, 0, 0, 1, rseed);
		global_building_params.restore_prev_pos_range();
		max_extent = max_extent.max(bc.get_max_extent());
		return 1;
	}
	bool remove_tile(int x, int y) {
		auto it(tiles.find(make_pair(x, y)));
		//cout << "Remove building tile " << x << "," << y << ", tiles: " << tiles.size() << endl;
		if (it == tiles.end()) return 0; // not found
		it->second.clear_vbos(); // free VBOs/VAOs
		tiles.erase(it);
		return 1;
	}
	void clear_vbos() {
		for (auto i = tiles.begin(); i != tiles.end(); ++i) {i->second.clear_vbos();}
	}
	void clear() {
		clear_vbos();
		tiles.clear();
	}
	bool check_sphere_coll(point &pos, point const &p_last, float radius, bool xy_only=0, vector3d *cnorm=nullptr) const {
		for (auto i = tiles.begin(); i != tiles.end(); ++i) {
			if (i->second.check_sphere_coll(pos, p_last, radius, xy_only, cnorm)) return 1;
		}
		return 0;
	}
	void add_drawn(vector3d const &xlate, vector<building_creator_t *> &bcs) {
		float const draw_dist(get_draw_tile_dist());
		point const camera(get_camera_pos() - xlate);

		for (auto i = tiles.begin(); i != tiles.end(); ++i) {
			//if (!i->second.get_bcube().closest_dist_xy_less_than(camera, draw_dist)) continue; // distance test (conservative)
			if (!dist_xy_less_than(camera, i->second.get_bcube().get_cube_center(), draw_dist)) continue; // distance test (aggressive)
			if (i->second.is_visible(xlate)) {bcs.push_back(&i->second);}
		}
	}
	unsigned get_tot_num_buildings() const {
		unsigned num(0);
		for (auto i = tiles.begin(); i != tiles.end(); ++i) {num += i->second.get_num_buildings();}
		return num;
	}
}; // end building_tiles_t


building_creator_t building_creator, building_creator_city;
building_tiles_t building_tiles;

void create_buildings_tile(int x, int y) {
	if (global_building_params.gen_inf_buildings()) {building_tiles.create_tile(x, y);}
}
void remove_buildings_tile(int x, int y) {
	if (global_building_params.gen_inf_buildings()) {building_tiles.remove_tile(x, y);}
}

vector3d get_tt_xlate_val() {return ((world_mode == WMODE_INF_TERRAIN) ? vector3d(xoff*DX_VAL, yoff*DY_VAL, 0.0) : zero_vector);}

void gen_buildings() {
	update_sun_and_moon(); // need to update light_factor from sun to know if we need to generate window light geometry

	if (world_mode == WMODE_INF_TERRAIN && have_cities()) {
		building_creator_city.gen(global_building_params, 1, 0, 0); // city buildings
		global_building_params.restore_prev_pos_range(); // hack to undo clip to city bounds to allow buildings to extend further out
		building_creator.gen     (global_building_params, 0, 1, 0); // non-city secondary buildings
	} else {building_creator.gen (global_building_params, 0, 0, 0);} // mixed buildings
}
void draw_buildings(int shadow_only, vector3d const &xlate) {
	//if (!building_tiles.empty()) {cout << "Building Tiles: " << building_tiles.size() << " Tiled Buildings: " << building_tiles.get_tot_num_buildings() << endl;} // debugging
	if (world_mode != WMODE_INF_TERRAIN) {building_tiles.clear();}
	vector<building_creator_t *> bcs;
	if (world_mode == WMODE_INF_TERRAIN && building_creator_city.is_visible(xlate)) {bcs.push_back(&building_creator_city);}
	if (shadow_only != 2 && building_creator.is_visible(xlate)) {bcs.push_back(&building_creator);} // don't draw secondary buildings for dynamic shadows
	building_tiles.add_drawn(xlate, bcs);
	building_creator_t::multi_draw(shadow_only, xlate, bcs);
}
void draw_building_lights(vector3d const &xlate) {
	building_creator_city.draw_building_lights(xlate);
	//building_creator.draw_building_lights(xlate); // only city buildings for now
}
bool proc_buildings_sphere_coll(point &pos, point const &p_int, float radius, bool xy_only, vector3d *cnorm) {
	// we generally won't intersect more than one of these categories, so we can return true without checking all cases
	return (building_creator_city.check_sphere_coll(pos, p_int, radius, xy_only, cnorm) ||
		         building_creator.check_sphere_coll(pos, p_int, radius, xy_only, cnorm) ||
		           building_tiles.check_sphere_coll(pos, p_int, radius, xy_only, cnorm));
}
bool check_buildings_sphere_coll(point const &pos, float radius, bool apply_tt_xlate, bool xy_only) {
	point center(pos);
	if (apply_tt_xlate) {center += get_tt_xlate_val();} // apply xlate for all static objects - not the camera
	return proc_buildings_sphere_coll(center, pos, radius, xy_only, nullptr);
}
bool check_buildings_point_coll(point const &pos, bool apply_tt_xlate, bool xy_only) {
	return check_buildings_sphere_coll(pos, 0.0, apply_tt_xlate, xy_only);
}
bool check_buildings_no_grass(point const &pos) { // for tiled terrain mode
	point center(pos + get_tt_xlate_val());
	return building_creator.check_sphere_coll(center, pos, 0.0, 1, nullptr); // secondary buildings only
}
unsigned check_buildings_line_coll(point const &p1, point const &p2, float &t, unsigned &hit_bix, bool apply_tt_xlate, bool ret_any_pt) { // for line_intersect_city()
	vector3d const xlate(apply_tt_xlate ? get_tt_xlate_val() : zero_vector);
	unsigned const coll1(building_creator_city.check_line_coll(p1+xlate, p2+xlate, t, hit_bix, ret_any_pt, 0));
	if (coll1 && ret_any_pt) return coll1;
	unsigned const coll2(building_creator.check_line_coll(p1+xlate, p2+xlate, t, hit_bix, ret_any_pt, 1));
	return (coll2 ? coll2 : coll1);
}
bool get_buildings_line_hit_color(point const &p1, point const &p2, colorRGBA &color) {
	if (world_mode == WMODE_INF_TERRAIN && building_creator_city.get_building_hit_color(p1, p2, color)) return 1;
	return building_creator.get_building_hit_color(p1, p2, color);
}
bool have_buildings() {return (!building_creator.empty() || !building_creator_city.empty() || !building_tiles.empty());} // for postproce effects
bool no_grass_under_buildings() {return (world_mode == WMODE_INF_TERRAIN && !building_creator.empty() && global_building_params.flatten_mesh);}
unsigned get_buildings_gpu_mem_usage() {return (building_creator.get_gpu_mem_usage() + building_creator_city.get_gpu_mem_usage());}

vector3d get_buildings_max_extent() { // used for TT shadow bounds + map mode
	return building_creator.get_max_extent().max(building_creator_city.get_max_extent()).max(building_tiles.get_max_extent());
}
void clear_building_vbos() {
	building_creator.clear_vbos();
	building_creator_city.clear_vbos();
	building_tiles.clear_vbos();
}

// city interface
void set_buildings_pos_range(cube_t const &pos_range) {global_building_params.set_pos_range(pos_range);}
void get_building_bcubes(cube_t const &xy_range, vect_cube_t &bcubes) {building_creator_city.get_overlapping_bcubes(xy_range, bcubes);} // Note: no xlate applied
// cars + peds
void get_building_occluders(pos_dir_up const &pdu, building_occlusion_state_t &state) {building_creator_city.get_occluders(pdu, state);}
bool check_pts_occluded(point const *const pts, unsigned npts, building_occlusion_state_t &state) {return building_creator_city.check_pts_occluded(pts, npts, state);}
// used for pedestrians
cube_t get_building_bcube(unsigned building_id) {return building_creator_city.get_building_bcube(building_id);}
bool check_line_coll_building(point const &p1, point const &p2, unsigned building_id) {return building_creator_city.check_line_coll_building(p1, p2, building_id);}
int get_building_bcube_contains_pos(point const &pos) {return building_creator_city.get_building_bcube_contains_pos(pos);}
bool check_buildings_ped_coll(point const &pos, float radius, unsigned plot_id, unsigned &building_id) {return building_creator_city.check_ped_coll(pos, radius, plot_id, building_id);}
bool select_building_in_plot(unsigned plot_id, unsigned rand_val, unsigned &building_id) {return building_creator_city.select_building_in_plot(plot_id, rand_val, building_id);}

