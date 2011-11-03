// 3D World - OpenGL CS184 Computer Graphics Project - collision detection BSP/KD/Oct Tree
// by Frank Gennari
// 10/16/10

#include "3DWorld.h"
#include "physics_objects.h"


bool const BUILD_COBJ_TREE   = 1;
unsigned const MAX_LEAF_SIZE = 2;
float const POLY_TOLER       = 1.0E-6;
float const OVERLAP_AMT      = 0.02;


extern int display_mode, frame_counter, cobj_counter;
extern vector<coll_obj> coll_objects;
extern vector<unsigned> falling_cobjs;
extern platform_cont platforms;


struct coll_tquad { // size = 60

	point pts[4];
	vector3d normal;
	unsigned cid, npts;

	coll_tquad() {}

	coll_tquad(coll_obj const &c) : normal(c.norm), cid(c.id), npts(c.npoints) {
		assert(is_cobj_valid(c));
		for (unsigned i = 0; i < npts; ++i) {pts[i] = c.points[i];}
	}
	static bool is_cobj_valid(coll_obj const &c) {
		return (!c.disabled() && c.type == COLL_POLYGON && (c.npoints == 3 || c.npoints == 4) && c.thickness <= MIN_POLY_THICK);
	}
	cube_t get_bounding_cube() const {
		cube_t cube(pts[0].x, pts[0].x, pts[0].y, pts[0].y, pts[0].z, pts[0].z);

		for (unsigned i = 1; i < npts; ++i) {
			UNROLL_3X(cube.d[i_][0] = min(cube.d[i_][0], pts[i][i_]); cube.d[i_][1] = max(cube.d[i_][1], pts[i][i_]););
		}
		UNROLL_3X(cube.d[i_][0] -= POLY_TOLER; cube.d[i_][1] += POLY_TOLER;);
		return cube;
	}
	bool line_intersect(point const &p1, point const &p2) const {
		//if (!check_line_clip(p1, p2, get_bounding_cube().d)) return 0;
		float t;
		return line_poly_intersect(p1, p2, pts, npts, normal, t);
	}
	bool line_int_exact(point const &p1, point const &p2, float &t, vector3d &cnorm, float tmin, float tmax) const {
		if (!line_poly_intersect(p1, p2, pts, npts, normal, t) || t > tmax || t < tmin) return 0;
		cnorm = get_poly_dir_norm(normal, p1, (p2 - p1), t);
		return 1;
	}
};


class cobj_tree_base {

protected:
	struct tree_node : public cube_t { // size = 36
		unsigned start, end; // index into cixs for leaves
		unsigned next_node_id;

		tree_node(unsigned s=0, unsigned e=0) : start(s), end(e), next_node_id(0) {
			UNROLL_3X(d[i_][0] = d[i_][1] = 0.0;)
		}
	};

	vector<tree_node> nodes;
	unsigned max_depth, max_leaf_count, num_leaf_nodes;

	inline void register_leaf(unsigned num) {
		++num_leaf_nodes;
		max_leaf_count = max(max_leaf_count, num);
	}

	struct node_ix_mgr {
		point const p1, p2;
		vector3d dinv;
		vector<tree_node> const &nodes;

		node_ix_mgr(vector<tree_node> const &nodes_, point const &p1_, point const &p2_)
			: nodes(nodes_), p1(p1_), p2(p2_), dinv(p2 - p1) {dinv.invert(0, 1);}

		bool check_node(unsigned &nix) const {
			tree_node const &n(nodes[nix]);

			if (!get_line_clip2(p1, dinv, n.d)) {
				assert(n.next_node_id > nix);
				nix = n.next_node_id; // failed the bbox test
				assert(nix > 0);
				return 0;
			}
			++nix;
			return 1;
		}
	};

public:
	bool is_empty() const {return nodes.empty();}
	void clear() {nodes.resize(0);}
};


class cobj_tree_triangles_t : public cobj_tree_base { // unused

	vector<coll_tquad> tquads, temp_bins[3];

	void calc_node_bbox(tree_node &n) const {
		assert(n.start < n.end && n.end <= tquads.size());
		n.copy_from(tquads[n.start].get_bounding_cube());

		for (unsigned i = n.start+1; i < n.end; ++i) { // bbox union
			n.union_with_cube(tquads[i].get_bounding_cube());
		}
	}

	void build_tree(unsigned nix, unsigned skip_dims, unsigned depth) {
		assert(nix < nodes.size());
		tree_node &n(nodes[nix]);
		assert(n.start < n.end);
		calc_node_bbox(n);
		unsigned const num(n.end - n.start);
		max_depth = max(max_depth, depth);
		
		if (num <= MAX_LEAF_SIZE || skip_dims == 7) { // base case
			register_leaf(num);
			return;
		}

		// determine split dimension and value
		unsigned dim(0);
		float max_sz(0);
#if 0
		float best_w(0), sval(0), slen(0);
		vector<pair<float, char> > vals[3]; // position, area
		vector3d dim_sizes;
		UNROLL_3X(dim_sizes[i_] = (n.d[i_][1] - n.d[i_][0]);)

		for (unsigned i = n.start; i < n.end; ++i) {
			cube_t const cube(tquads[i].get_bounding_cube());

			for (unsigned d = 0; d < 2; ++d) {
				UNROLL_3X(if (!(skip_dims & (1 << i_))) {vals[i_].push_back(make_pair(cube.d[i_][d], (d ? -1 : 1)));})
			}
		}
		for (unsigned d = 0; d < 3; ++d) {
			if (dim_sizes[d] == 0 || (skip_dims & (1 << d))) continue;
			sort(vals[d].begin(), vals[d].end());
			int in_both(0), in_left(0), in_right(num);
			cube_t left_cube(n), right_cube(n);

			for (unsigned i = 0; i < vals[d].size(); ++i) {
				float const val(vals[d][i].first);
				int const w(vals[d][i].second);
				in_both += w;
				((w > 0) ? in_left : in_right) += w;
				if (i+1 < vals[d].size() && val == vals[d][i+1].first)   continue; // duplicate value
				if (in_left == num || in_right == num || in_both == num) continue; // don't want to consider this
				left_cube.d[d][1]  = val;
				right_cube.d[d][0] = val;
				float const weight(left_cube.get_area()*(in_left + in_both) + right_cube.get_area()*(in_right + in_both));

				if (best_w == 0 || weight < best_w) {
					best_w = weight;
					max_sz = dim_sizes[d];
					sval   = val;
					slen   = (val - n.d[d][0])/max_sz;
					dim    = d;
				}
			}
			assert(in_left == num && in_right == 0 && in_both == 0);
		}
#else
		point const center(n.get_cube_center());

		for (unsigned d = 0; d < 3; ++d) {
			if (skip_dims & (1 << d)) continue;
			float const dim_sz(n.d[d][1] - n.d[d][0]);
			assert(dim_sz >= 0.0);
		
			if (dim_sz > max_sz) {
				max_sz = dim_sz;
				dim    = d;
			}
		}
		float const sval(center[dim]); // center point (mean seems to work better than median)
#endif
		if (max_sz == 0) { // can't split
			register_leaf(num);
			return;
		}
		assert(!(skip_dims & (1 << dim)));
		float const sval_lo(sval+OVERLAP_AMT*max_sz), sval_hi(sval-OVERLAP_AMT*max_sz);
		unsigned pos(n.start), bin_count[3] = {0};

		// split in this dimension
		for (unsigned i = n.start; i < n.end; ++i) {
			unsigned bix(2);
			cube_t const cube(tquads[i].get_bounding_cube());
			assert(cube.d[dim][0] <= cube.d[dim][1]);
			if (cube.d[dim][1] <= sval_lo) bix =  (depth&1); // ends   before the split, put in bin 0
			if (cube.d[dim][0] >= sval_hi) bix = !(depth&1); // starts after  the split, put in bin 1
			++bin_count[bix];
			temp_bins[bix].push_back(tquads[i]);
		}
		for (unsigned d = 0; d < 3; ++d) {
			for (unsigned i = 0; i < temp_bins[d].size(); ++i) {
				tquads[pos++] = temp_bins[d][i];
			}
			temp_bins[d].resize(0);
		}
		assert(pos == n.end);

		if (depth < 3) {
			cout << "d: " << depth << ", dim: " << dim << ", sval: " << sval << ", n: " << num << ", bc: " << bin_count[0] << " " << bin_count[1] << " " << bin_count[2] << endl;
			//cout << "slen: " << slen << ", max_sz: " << max_sz << ", bw: " << best_w << endl;
		}

		// check that dataset has been subdivided (not all in one bin)
		if (bin_count[0] == num || bin_count[1] == num || bin_count[2] == num) {
			build_tree(nix, (skip_dims | (1 << dim)), depth); // single bin, rebin with a different dim
			return;
		}

		// create child nodes and call recursively
		unsigned cur(n.start);

		for (unsigned bix = 0; bix < 3; ++bix) { // Note: this loop will invalidate the reference to 'n'
			unsigned const count(bin_count[bix]);
			if (count == 0) continue; // empty bin
			unsigned const kid(nodes.size());
			nodes.push_back(tree_node(cur, cur+count));
			build_tree(kid, skip_dims, depth+1);
			nodes[kid].next_node_id = nodes.size();
			cur += count;
		}
		assert(cur == nodes[nix].end);
		nodes[nix].start = nodes[nix].end = 0; // branch node has no leaves
	}

public:
	void clear() {
		cobj_tree_base::clear();
		tquads.clear(); // reserve(0)?
	}

	void add_cobjs(vector<coll_obj> const &cobjs, bool verbose) {
		RESET_TIME;
		clear();
		tquads.reserve(cobjs.size()); // is this a good idea?
		
		for (vector<coll_obj>::const_iterator i = cobjs.begin(); i != cobjs.end(); ++i) {
			if (i->status != COLL_STATIC) continue;
			assert(i->type == COLL_POLYGON && i->thickness <= MIN_POLY_THICK);
			tquads.push_back(coll_tquad(*i));
		}
		nodes.reserve(6*tquads.size()/5); // conservative
		nodes.push_back(tree_node(0, tquads.size()));
		assert(nodes.size() == 1);
		max_depth = max_leaf_count = num_leaf_nodes = 0;
		build_tree(0, 0, 0);
		nodes[0].next_node_id = nodes.size();

		if (verbose) {
			PRINT_TIME(" Cobj Tree Triangles Create");
			cout << "cobjs: " << cobjs.size() << ", leaves: " << tquads.size() << ", nodes: " << nodes.size() 
				 << ", depth: " << max_depth << ", max_leaf: " << max_leaf_count << ", leaf_nodes: " << num_leaf_nodes << endl;
		}
		//exit(0); // TESTING
	}

	bool check_coll_line(point const &p1, point const &p2, point &cpos, vector3d &cnorm, int &cindex, int ignore_cobj, bool exact) const {

		//static unsigned num_checks(0), num_node(0), num_tests(0), num_hits(0), num_success(0);
		//++num_checks;
		cindex = -1;
		if (nodes.empty()) return 0;
		bool ret(0);
		float t(0.0), tmin(0.0), tmax(1.0);
		node_ix_mgr nixm(nodes, p1, p2);
		unsigned const num_nodes(nodes.size());

		for (unsigned nix = 0; nix < num_nodes;) {
			//++num_node;
			tree_node const &n(nodes[nix]);
			if (!nixm.check_node(nix)) continue;

			for (unsigned i = n.start; i < n.end; ++i) { // check leaves
				// Note: we test cobj against the original (unclipped) p1 and p2 so that t is correct
				// Note: we probably don't need to return cnorm and cpos in inexact mode, but it shouldn't be too expensive to do so
				//++num_tests;
				if ((int)tquads[i].cid == ignore_cobj) continue;
				if (!tquads[i].line_int_exact(p1, p2, t, cnorm, tmin, tmax)) continue;
				//++num_hits;
				cindex = tquads[i].cid;
				cpos   = p1 + (p2 - p1)*t;
				if (!exact) return 1; // return first hit
				nixm.dinv = vector3d(cpos - p1);
				nixm.dinv.invert(0, 1);
				tmax = t;
				ret  = 1;
			}
		}
		//if (ret) ++num_success;
		//if ((num_checks % 10000) == 0) {cout << "nc: " << num_checks << ", nn: " << num_node << ", nt: " << num_tests << ", nh: " << num_hits << ", ns: " << num_success << endl;}
		return ret;
	}
}; // cobj_tree_triangles_t


// 3: BSP Tree/KD-Tree, 8: OctTree
template<unsigned NUM> class cobj_tree_t : public cobj_tree_base {

	vector<coll_obj> const &cobjs;
	vector<unsigned> cixs, temp_bins[NUM];
	bool is_static, is_dynamic, occluders_only, moving_only;

	void add_cobj(unsigned ix) {if (obj_ok(cobjs[ix])) cixs.push_back(ix);}
	inline coll_obj const &get_cobj(unsigned ix) const {return cobjs[cixs[ix]];}

	// Note: start by adding all cobjs to [start, end], then calc bbox, then split into branches and leaves
	void calc_node_bbox(tree_node &n) const {
		assert(n.start < n.end);
		n.copy_from(get_cobj(n.start));

		for (unsigned i = n.start+1; i < n.end; ++i) { // bbox union
			n.union_with_cube(get_cobj(i));
		}
	}

	bool create_cixs() {
		if (moving_only) {
			for (platform_cont::const_iterator i = platforms.begin(); i != platforms.end(); ++i) {
				for (vector<unsigned>::const_iterator j = i->cobjs.begin(); j != i->cobjs.end(); ++j) {
					add_cobj(*j);
				}
			}
			for (unsigned i = 0; i < falling_cobjs.size(); ++i) {
				add_cobj(falling_cobjs[i]);
			}
		}
		else {
			if (is_static && !occluders_only) cixs.reserve(cobjs.size());

			for (unsigned i = 0; i < cobjs.size(); ++i) {
				add_cobj(i);
			}
		}
		assert(cixs.size() < (1 << 29));
		return !cixs.empty();
	}

	void build_tree(unsigned nix, unsigned skip_dims, unsigned depth) {assert(0);}

	bool obj_ok(coll_obj const &c) const {
		return (((is_static && c.status == COLL_STATIC) || (is_dynamic && c.status == COLL_DYNAMIC)) &&
			(!occluders_only || c.is_occluder()) && (!moving_only || c.maybe_is_moving()));
	}


public:
	cobj_tree_t(vector<coll_obj> const &cobjs_, bool s, bool d, bool o, bool m)
		: cobjs(cobjs_), is_static(s), is_dynamic(d), occluders_only(o), moving_only(m) {}

	void clear() {
		cobj_tree_base::clear();
		cixs.resize(0);
	}

	void add_cobjs(bool verbose) {
		RESET_TIME;
		clear();
		if (!create_cixs()) return; // nothing to be done
		nodes.reserve(6*cixs.size()/5); // conservative
		nodes.push_back(tree_node(0, cixs.size()));
		assert(nodes.size() == 1);
		max_depth = max_leaf_count = num_leaf_nodes = 0;
		build_tree(0, 0, 0);
		nodes[0].next_node_id = nodes.size();

		if (verbose) {
			PRINT_TIME(" Cobj Tree Create");
			cout << "cobjs: " << cobjs.size() << ", leaves: " << cixs.size() << ", nodes: " << nodes.size()
				 << ", depth: " << max_depth << ", max_leaves: " << max_leaf_count << ", leaf_nodes: " << num_leaf_nodes << endl;
		}
	}

	bool check_coll_line(point const &p1, point const &p2, point &cpos, vector3d &cnorm, int &cindex, int ignore_cobj,
		bool exact, int test_alpha, bool skip_non_drawn) const
	{
		cindex = -1;
		if (nodes.empty()) return 0;
		assert(test_alpha != 2); // don't support this mode
		bool ret(0);
		float t(0.0), tmin(0.0), tmax(1.0);
		node_ix_mgr nixm(nodes, p1, p2);
		unsigned const num_nodes(nodes.size());

		for (unsigned nix = 0; nix < num_nodes;) {
			tree_node const &n(nodes[nix]);
			if (!nixm.check_node(nix)) continue;

			for (unsigned i = n.start; i < n.end; ++i) { // check leaves
				// Note: we test cobj against the original (unclipped) p1 and p2 so that t is correct
				// Note: we probably don't need to return cnorm and cpos in inexact mode, but it shouldn't be too expensive to do so
				if ((int)cixs[i] == ignore_cobj)  continue;
				coll_obj const &c(get_cobj(i));
				if (!obj_ok(c))                   continue;
				if (skip_non_drawn && !c.might_be_drawn())                  continue;
				if (test_alpha == 1 && c.is_semi_trans())                   continue; // semi-transparent, can see through
				if (test_alpha == 3 && c.cp.color.alpha < MIN_SHADOW_ALPHA) continue; // less than min alpha
				if (!c.line_int_exact(p1, p2, t, cnorm, tmin, tmax))        continue;
				cindex = cixs[i];
				cpos   = p1 + (p2 - p1)*t;
				if (!exact) return 1; // return first hit
				nixm.dinv = vector3d(cpos - p1);
				nixm.dinv.invert(0, 1);
				tmax = t;
				ret  = 1;
			}
		}
		return ret;
	}

	void get_intersecting_cobjs(cube_t const &cube, vector<unsigned> &cobjs, int ignore_cobj, float toler, bool check_ccounter, int id_for_cobj_int) const {
		unsigned const num_nodes(nodes.size());

		for (unsigned nix = 0; nix < num_nodes;) {
			tree_node const &n(nodes[nix]);
			assert(n.start <= n.end);

			if (!cube.intersects(n, toler)) {
				assert(n.next_node_id > nix);
				nix = n.next_node_id; // failed the bbox test
				assert(nix > 0);
				continue;
			}
			for (unsigned i = n.start; i < n.end; ++i) { // check leaves
				if ((int)cixs[i] == ignore_cobj) continue;
				coll_obj const &c(get_cobj(i));
				if (check_ccounter && c.counter == cobj_counter) continue;
				// get_intersecting_cobjs_tree() calls this on both static and moving cobj_trees, so we want to check to make sure we don't double include it
				if (!moving_only && c.maybe_is_moving())         continue;
				if (!obj_ok(c) || !cube.intersects(c, toler))    continue;
				if (id_for_cobj_int >= 0 && coll_objects[id_for_cobj_int].intersects_cobj(c, toler) != 1) continue;
				cobjs.push_back(cixs[i]);
			}
			++nix;
		}
	}

	bool is_cobj_contained(point const &p1, point const &p2, point const &viewer, point const *const pts, unsigned npts, int ignore_cobj, int &cobj) const {
		if (nodes.empty()) return 0;
		node_ix_mgr nixm(nodes, p1, p2);
		unsigned const num_nodes(nodes.size());

		for (unsigned nix = 0; nix < num_nodes;) {
			tree_node const &n(nodes[nix]);
			if (!nixm.check_node(nix)) continue;

			for (unsigned i = n.start; i < n.end; ++i) { // check leaves
				if ((int)cixs[i] == ignore_cobj) continue;
				coll_obj const &c(get_cobj(i));
				
				if (obj_ok(c) && c.is_occluder() && c.intersects_all_pts(viewer, pts, npts)) {
					cobj = cixs[i];
					return 1;
				}
			}
		}
		return 0;
	}

	void get_coll_line_cobjs(point const &pos1, point const &pos2, int ignore_cobj, vector<int> &cobjs) const {
		if (nodes.empty()) return;
		node_ix_mgr nixm(nodes, pos1, pos2);
		unsigned const num_nodes(nodes.size());

		for (unsigned nix = 0; nix < num_nodes;) {
			tree_node const &n(nodes[nix]);
			if (!nixm.check_node(nix)) continue;
			
			for (unsigned i = n.start; i < n.end; ++i) { // check leaves
				if ((int)cixs[i] == ignore_cobj) continue;
				coll_obj const &c(get_cobj(i));
				if (obj_ok(c) && c.is_big_occluder() && check_line_clip_expand(pos1, pos2, c.d, GET_OCC_EXPAND)) cobjs.push_back(cixs[i]);
			}
		}
	}
}; // cobj_tree_t


// BSP Tree/KD-Tree (left, right, mid) kids
template <> void cobj_tree_t<3>::build_tree(unsigned nix, unsigned skip_dims, unsigned depth) {
	
	assert(nix < nodes.size());
	tree_node &n(nodes[nix]);
	assert(n.start < n.end);
	calc_node_bbox(n);
	unsigned const num(n.end - n.start);
	max_depth = max(max_depth, depth);
	
	if (num <= MAX_LEAF_SIZE || skip_dims == 7) { // base case
		register_leaf(num);
		return;
	}
	
	// determine split dimension and value
	point const center(n.get_cube_center());
	unsigned dim(0);
	float max_sz(0);

	for (unsigned d = 0; d < 3; ++d) {
		if (skip_dims & (1 << d)) continue;
		float const dim_sz(n.d[d][1] - n.d[d][0]);
		assert(dim_sz >= 0.0);
		
		if (dim_sz > max_sz) {
			max_sz = dim_sz;
			dim    = d;
		}
	}
	if (max_sz == 0) { // can't split
		register_leaf(num);
		return;
	}
	assert(!(skip_dims & (1 << dim)));
	float const sval(center[dim]); // center point (mean seems to work better than median)
	float const sval_lo(sval+OVERLAP_AMT*max_sz), sval_hi(sval-OVERLAP_AMT*max_sz);
	unsigned pos(n.start), bin_count[3] = {0};

	// split in this dimension: use upper 2 bits of cixs for storing bin index
	for (unsigned i = n.start; i < n.end; ++i) {
		unsigned bix(2);
		coll_obj const &cobj(get_cobj(i));
		assert(cobj.d[dim][0] <= cobj.d[dim][1]);
		if (cobj.d[dim][1] <= sval_lo) bix =  (depth&1); // ends   before the split, put in bin 0
		if (cobj.d[dim][0] >= sval_hi) bix = !(depth&1); // starts after  the split, put in bin 1
		++bin_count[bix];
		temp_bins[bix].push_back(cixs[i]);
	}
	for (unsigned d = 0; d < 3; ++d) {
		for (unsigned i = 0; i < temp_bins[d].size(); ++i) {
			cixs[pos++] = temp_bins[d][i];
		}
		temp_bins[d].resize(0);
	}
	assert(pos == n.end);

	// check that dataset has been subdivided (not all in one bin)
	if (bin_count[0] == num || bin_count[1] == num || bin_count[2] == num) {
		build_tree(nix, (skip_dims | (1 << dim)), depth); // single bin, rebin with a different dim
		return;
	}

	// create child nodes and call recursively
	unsigned cur(n.start);

	for (unsigned bix = 0; bix < 3; ++bix) { // Note: this loop will invalidate the reference to 'n'
		unsigned const count(bin_count[bix]);
		if (count == 0) continue; // empty bin
		unsigned const kid(nodes.size());
		nodes.push_back(tree_node(cur, cur+count));
		build_tree(kid, skip_dims, depth+1);
		nodes[kid].next_node_id = nodes.size();
		cur += count;
	}
	assert(cur == nodes[nix].end);
	nodes[nix].start = nodes[nix].end = 0; // branch node has no leaves
}


// OctTree
template <> void cobj_tree_t<8>::build_tree(unsigned nix, unsigned skip_dims, unsigned depth) {

	assert(nix < nodes.size());
	tree_node &n(nodes[nix]);
	assert(n.start < n.end);
	calc_node_bbox(n);
	unsigned const num(n.end - n.start);
	max_depth = max(max_depth, depth);

	if (num <= MAX_LEAF_SIZE || skip_dims) { // base case
		register_leaf(num);
		return;
	}
	
	// determine split values
	point const sval(n.get_cube_center()); // center point
	unsigned pos(n.start), bin_count[8] = {0};

	// split in this dimension: use upper 3 bits of cixs for storing bin index
	for (unsigned i = n.start; i < n.end; ++i) {
		point const center(get_cobj(i).get_cube_center());
		unsigned bix(0);
		UNROLL_3X(if (center[i_] > sval[i_]) bix |= (1 << i_);)
		++bin_count[bix];
		temp_bins[bix].push_back(cixs[i]);
	}
	for (unsigned d = 0; d < 8; ++d) {
		for (unsigned i = 0; i < temp_bins[d].size(); ++i) {
			cixs[pos++] = temp_bins[d][i];
		}
		temp_bins[d].resize(0);
	}
	assert(pos == n.end);

	// create child nodes and call recursively
	unsigned cur(n.start);

	for (unsigned bix = 0; bix < 8; ++bix) { // Note: this loop will invalidate the reference to 'n'
		unsigned const count(bin_count[bix]);
		if (count == 0) continue; // empty bin
		unsigned const kid(nodes.size());
		nodes.push_back(tree_node(cur, cur+count));
		build_tree(kid, (count == num), depth+1); // if all in one bin, make that bin a leaf
		nodes[kid].next_node_id = nodes.size();
		cur += count;
	}
	assert(cur == nodes[nix].end);
	nodes[nix].start = nodes[nix].end = 0; // branch node has no leaves
}


// 3: BSP Tree/KD-Tree, 8: Octtree
typedef cobj_tree_t<3> cobj_tree_type;
cobj_tree_type cobj_tree_static (coll_objects, 1, 0, 0, 0);
cobj_tree_type cobj_tree_dynamic(coll_objects, 0, 1, 0, 0);
cobj_tree_type cobj_tree_occlude(coll_objects, 1, 0, 1, 0);
cobj_tree_type cobj_tree_moving (coll_objects, 1, 0, 0, 1);
cobj_tree_triangles_t cobj_tree_triangles;
int last_update_frame[2] = {1, 1}; // first drawn frame is 1


cobj_tree_type &get_tree(bool dynamic) {
	return (dynamic ? cobj_tree_dynamic : cobj_tree_static);
}

void build_cobj_tree(bool dynamic, bool verbose) {
	if (BUILD_COBJ_TREE) {
		get_tree(dynamic).add_cobjs(verbose);
		if (!dynamic) cobj_tree_occlude.add_cobjs(verbose);
		//if (!dynamic) cobj_tree_triangles.add_cobjs(coll_objects, verbose);
	}
}

void update_cobj_tree(bool dynamic, bool verbose) {

	if (dynamic && last_update_frame[dynamic] < frame_counter) {
		last_update_frame[dynamic] = frame_counter;
		build_cobj_tree(dynamic, verbose);
	}
}

void build_moving_cobj_tree() {
	cobj_tree_moving.add_cobjs(0);
}

// can use with ray trace lighting, snow collision?, maybe water reflections
bool check_coll_line_exact_tree(point const &p1, point const &p2, point &cpos,
								vector3d &cnorm, int &cindex, int ignore_cobj, bool dynamic, int test_alpha, bool skip_non_drawn)
{
	//return cobj_tree_triangles.check_coll_line(p1, p2, cpos, cnorm, cindex, ignore_cobj, 1);
	return get_tree(dynamic).check_coll_line(p1, p2, cpos, cnorm, cindex, ignore_cobj, 1, test_alpha, skip_non_drawn);
}

// can use with snow shadows, grass shadows, tree leaf shadows
bool check_coll_line_tree(point const &p1, point const &p2, int &cindex, int ignore_cobj, bool dynamic, int test_alpha, bool skip_non_drawn) {

	vector3d cnorm; // unused
	point cpos; // unused
	return get_tree(dynamic).check_coll_line(p1, p2, cpos, cnorm, cindex, ignore_cobj, 0, test_alpha, skip_non_drawn);
}


void get_intersecting_cobjs_tree(cube_t const &cube, vector<unsigned> &cobjs, int ignore_cobj, float toler,
	bool dynamic, bool check_ccounter, int id_for_cobj_int)
{
	get_tree(dynamic).get_intersecting_cobjs(cube, cobjs, ignore_cobj, toler, check_ccounter, id_for_cobj_int);
	cobj_tree_moving.get_intersecting_cobjs (cube, cobjs, ignore_cobj, toler, check_ccounter, id_for_cobj_int);
}


bool cobj_contained_tree(point const &p1, point const &p2, point const &viewer, point const *const pts,
	unsigned npts, int ignore_cobj, int &cobj)
{
	return cobj_tree_occlude.is_cobj_contained(p1, p2, viewer, pts, npts, ignore_cobj, cobj);
}


void get_coll_line_cobjs_tree(point const &pos1, point const &pos2, int ignore_cobj, vector<int> &cobjs) {

	cobj_tree_occlude.get_coll_line_cobjs(pos1, pos2, ignore_cobj, cobjs);
}


bool have_occluders() {

	return (BUILD_COBJ_TREE ? !cobj_tree_occlude.is_empty() : 1);
}




