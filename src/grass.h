// 3D World - grass_t and grass_manager classes
// by Frank Gennari
// 7/9/12

#ifndef _GRASS_H_
#define _GRASS_H_

#include "3DWorld.h"


class grass_manager_t {

protected:
	struct grass_t { // size = 48
		point p;
		vector3d dir, n;
		unsigned char c[3];
		unsigned char shadowed;
		float w;

		grass_t() {} // optimization
		grass_t(point const &p_, vector3d const &dir_, vector3d const &n_, unsigned char const *const c_, float w_)
			: p(p_), dir(dir_), n(n_), shadowed(0), w(w_) {c[0] = c_[0]; c[1] = c_[1]; c[2] = c_[2];}
		void merge(grass_t const &g);
	};

	vector<grass_t> grass;
	unsigned vbo;
	bool vbo_valid, data_valid;
	rand_gen_t rgen;
	typedef vert_norm_comp_tc_color grass_data_t;

public:
	grass_manager_t() : vbo(0), vbo_valid(0), data_valid(0) {}
	// can't free in the destructor because the gl context may be destroyed before this point
	//~grass_manager_t() {clear();}
	size_t size() const {return grass.size ();} // 2 points per grass blade
	bool empty()  const {return grass.empty();}
	void invalidate_vbo() {vbo_valid = 0;}
	void clear();
	void add_grass_blade(point const &pos, float cscale);
	void create_new_vbo();
	void add_to_vbo_data(grass_t const &g, vector<grass_data_t> &data, unsigned &ix, vector3d &norm) const;
	void begin_draw(float spec_weight) const;
	void end_draw() const;
};


#endif // _GRASS_H_
