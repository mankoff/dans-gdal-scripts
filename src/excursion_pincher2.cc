/*
Copyright (c) 2009, Regents of the University of Alaska

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of the Geographic Information Network of Alaska nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

This code was developed by Dan Stahlke for the Geographic Information Network of Alaska.
*/



#include <limits>
#include <cassert>

#include "common.h"
#include "polygon.h"
#include "debugplot.h"

#define DEBUG 0

namespace dangdal {

static inline double seg_ang(Vertex v0, Vertex v1) {
	double dx = v1.x - v0.x;
	double dy = v1.y - v0.y;
	return atan2(dy, dx);
}

static inline double seg_len(Vertex v0, Vertex v1) {
	double dx = v1.x - v0.x;
	double dy = v1.y - v0.y;
	return sqrt(dx*dx + dy*dy);
}

static size_t find_bottom_pt(const Ring &ring) {
	assert(ring.pts.size());
	double min = ring.pts[0].y;
	size_t min_idx = 0;
	for(size_t i=1; i<ring.pts.size(); i++) {
		double y = ring.pts[i].y;
		if(y<min) {
			min = y;
			min_idx = i;
		}
	}
	return min_idx;
}

struct FindNextConvexRetval {
	FindNextConvexRetval(
		bool _error, size_t _idx, double _ang
	) : error(_error), idx(_idx), ang(_ang) { }

	FindNextConvexRetval(
		size_t _idx, double _ang
	) : error(false), idx(_idx), ang(_ang) { }

	static const FindNextConvexRetval ERROR() {
		return FindNextConvexRetval(true, 0, 0);
	}

	bool error;
	size_t idx;
	double ang;
};

static FindNextConvexRetval find_next_convex(
	const Ring &ring, int start_idx, int limit_idx, double start_ang
) {
	int npts = ring.pts.size();
	const std::vector<Vertex> &pts = ring.pts;
	Vertex v0 = pts[start_idx];
	double min_angdiff = M_PI;
	double last_ad = -999;
	int best_vert = -1;
	double best_segang = 0;
	for(int i=(start_idx+1)%npts; ; i=(i+1)%npts) {
		if(i == start_idx) break;
		Vertex v1 = pts[i];
		double segang = seg_ang(v0, v1);
//printf("start_ang=%g*PI, seg_ang=%g*PI\n", start_ang/M_PI, segang/M_PI);
		double angdiff = segang - start_ang;
		while(angdiff < 0) angdiff += 2.0 * M_PI;
		while(angdiff >= 2.0*M_PI) angdiff -= 2.0 * M_PI;
		// FIXME - think about this some more
		if(last_ad != -999 && ((last_ad<M_PI) != (angdiff<M_PI))) {
			if(DEBUG) printf("test for seg crosses initial ray (%g*PI and %g*PI)\n", last_ad, angdiff);
			if(fabs(last_ad-angdiff) > M_PI) {
				if(DEBUG) printf("seg crosses initial ray (%g*PI and %g*PI)\n", last_ad, angdiff);
				return FindNextConvexRetval::ERROR();
			}
		}
		last_ad = angdiff;
		if(angdiff < min_angdiff) {
			min_angdiff = angdiff;
			best_vert = i;
			best_segang = segang;
		}
		if(i == limit_idx) break;
	}
	if(best_vert < 0) {
		return FindNextConvexRetval(limit_idx,
			std::numeric_limits<double>::signaling_NaN());
	} else if(min_angdiff >= M_PI) {
		if(DEBUG) printf("point on wrong side of half-plane (ang=%g*PI) idx=%d\n", min_angdiff/M_PI, best_vert);
		return FindNextConvexRetval::ERROR();
	} else {
		return FindNextConvexRetval(best_vert, best_segang);
	}
}

static bool *find_chull(const Ring &ring) {
	bool *keep = MYALLOC(bool, ring.pts.size());
	for(int i=0; i<ring.pts.size(); i++) keep[i] = false;

	int start_idx = find_bottom_pt(ring);
	keep[start_idx] = true;
	double ang = 0;
	size_t idx = start_idx;
	for(;;) {
		FindNextConvexRetval r = find_next_convex(ring, idx, start_idx, ang);
		if(r.error) fatal_error("could not get convex hull");
		idx = r.idx;
		ang = r.ang;
		if(idx == start_idx) break;
		keep[idx] = true;
	}

	return keep;
}

static double subring_area(const Ring &ring, int from, int to) {
	int npts = ring.pts.size();
	const std::vector<Vertex> &pts = ring.pts;

	double accum = 0;
	int i;
	for(i=from; ; i=(i+1)%npts) {
		int i2 = i==to ? from : (i+1)%npts;
		double x0 = pts[i].x;
		double y0 = pts[i].y;
		double x1 = pts[i2].x;
		double y1 = pts[i2].y;
		accum += x1*y0 - x0*y1;
		if(i == to) break;
	}
	//if(fabs(accum - round(accum)) > 1e-9) {
	//	fatal_error("accum not integer in subring_area (%g)", accum);
	//}
	if(accum < 0) {
		fatal_error("subring_area was negative");
	}
	return accum / 2.0;
}

static size_t next_keep(size_t npts, bool *keep, size_t i) {
	size_t i_plus1 = (i+1<npts) ? (i+1) : 0;
	for(int j=i_plus1; j<npts; j++) {
		if(keep[j]) return j;
	}
	for(int j=0; j<i; j++) {
		if(keep[j]) return j;
	}
	return i;
}

static int prev_keep(int npts, bool *keep, int i) {
	for(int j=(i+npts-1)%npts; j!=i; j=(j+npts-1)%npts) {
		if(keep[j]) return j;
	}
	return i;
}

static bool reach_point(const Ring &ring, bool *keep, int from, int to, double ang) {
	int npts = ring.pts.size();
	const std::vector<Vertex> &pts = ring.pts;

	if(DEBUG) printf("  reach %d and %d\n", from, to);
	if(DEBUG) printf("  %g,%g : %g,%g ang=%g*PI\n",
		pts[from].x, pts[from].y, pts[to].x, pts[to].y, ang/M_PI);

	int idx = from;
	for(;;) {
		FindNextConvexRetval r = find_next_convex(ring, idx, to, ang);
		if(r.error) return 1;
		idx = r.idx;
		ang = r.ang;
		keep[idx] = true;
		if(idx == to) break;
	}
	for(int pk=from; pk!=to;) {
		int nk = next_keep(npts, keep, pk);
		if(DEBUG) printf("test seg %g,%g : %g,%g\n", pts[pk].x, pts[pk].y, pts[nk].x, pts[nk].y);

		double min_x = std::min(pts[pk].x, pts[nk].x);
		double max_x = std::max(pts[pk].x, pts[nk].x);
		double min_y = std::min(pts[pk].y, pts[nk].y);
		double max_y = std::max(pts[pk].y, pts[nk].y);

		// FIXME - this is the slowest part
		// FIXME - doesn't handle crossing across a vertex here or in dp.c
		for(int i=0; i<npts; i++) {
			int i2 = i+1<npts ? i+1 : 0;
			Vertex p1 = pts[i];
			Vertex p2 = pts[i2];
			// the bbox test is copied here from line_intersects_line to save
			// the time penalty of a function call for the common case of
			// the bbox not matching
			if(!(
				max_x < std::min(p1.x, p2.x) ||
				min_x > std::max(p1.x, p2.x) ||
				max_y < std::min(p1.y, p2.y) ||
				min_y > std::max(p1.y, p2.y) ||
				i==pk || i==nk || i2==pk || i2==nk
			)) {
				if(line_intersects_line(pts[pk], pts[nk], p1, p2, 0)) {
					if(DEBUG) printf("line intersects line\n");
					return 1;
				}
			}
		}
		for(int i=nk; ; ) {
			int i2 = next_keep(npts, keep, i);
			Vertex p1 = pts[i];
			Vertex p2 = pts[i2];
			// the bbox test is copied here from line_intersects_line to save
			// the time penalty of a function call for the common case of
			// the bbox not matching
			if(!(
				max_x < std::min(p1.x, p2.x) ||
				min_x > std::max(p1.x, p2.x) ||
				max_y < std::min(p1.y, p2.y) ||
				min_y > std::max(p1.y, p2.y) ||
				i==pk || i==nk || i2==pk || i2==nk
			)) {
				if(line_intersects_line(pts[pk], pts[nk], p1, p2, 0)) {
					if(DEBUG) printf("line intersects line\n");
					return 1;
				}
			}
			i = i2;
			if(i == nk) break;
		}

		pk = nk;
	}
	if(DEBUG) printf("pass\n");
	return 0;
}

static bool add_tiepoint(const Ring &ring, bool *keep, int mid) {
	int npts = ring.pts.size();
	const std::vector<Vertex> &pts = ring.pts;

	keep[mid] = true;
	int left = prev_keep(npts, keep, mid);
	int right = next_keep(npts, keep, mid);
	if(DEBUG) printf("adding %d between %d and %d\n", mid, left, right);
	if(DEBUG) printf("%g,%g : %g,%g : %g,%g\n",
		pts[mid].x, pts[mid].y, pts[left].x, pts[left].y, pts[right].x, pts[right].y);

	double ang = seg_ang(pts[left], pts[right]);
	bool error = reach_point(ring, keep, left, mid, ang);
	if(error) return 1;

	int pk = prev_keep(npts, keep, mid);
	if(pk == mid) fatal_error("pk == mid");
	ang = seg_ang(pts[mid], pts[pk]);
	error = reach_point(ring, keep, mid, right, ang);
	if(error) return 1;

	return 0;
}

/*
static int chord_crosses_arc(const Ring &ring, int c0, int c1, int from, int to) {
	int npts = ring.pts.size();
	const std::vector<Vertex> &pts = ring.pts;

	for(int i=from; i!=to; i=(i+1)%npts) {
		int i2 = (i+1)%npts;
		if(i==c0 || i==c1 || i2==c0 || i2==c1) continue;
		if(line_intersects_line(pts[c0], pts[c1], pts[i], pts[i2], 0)) return 1;
	}
	return 0;
}
*/

// distance of p3 from line (p1,p2)
static double dist_to_seg(Vertex p1, Vertex p2, Vertex p3) {
	double d21x = p2.x - p1.x; double d21y = p2.y - p1.y;
	double d13x = p1.x - p3.x; double d13y = p1.y - p3.y;
	// from http://mathworld.wolfram.com/Point-LineDistance2-Dimensional.html
	double dist_from_line = fabs(d21x*d13y - d13x*d21y) / sqrt(d21x*d21x + d21y*d21y);
	return dist_from_line;
}

static int is_mostly_linear(const Ring &ring, int from, int to) {
	int npts = ring.pts.size();
	const std::vector<Vertex> &pts = ring.pts;

	Vertex p1 = pts[from];
	Vertex p2 = pts[to];
	for(int i=(from+1)%npts; i!=to; i=(i+1)%npts) {
		double dist = dist_to_seg(p1, p2, pts[i]);
		if(dist > 1.0) return 0;
	}

	return 1;
}

static int keep_linears(const Ring &ring, bool *keep_orig, int from, int to, bool *touchpts) {
	int npts = ring.pts.size();
	const std::vector<Vertex> &pts = ring.pts;

	if(to == (from+1)%npts) return 0;

	bool *keep_new = MYALLOC(bool, npts);

	double min_length = 20; // FIXME

	for(int l_idx=from; l_idx!=to; l_idx=(l_idx+1)%npts) {
		int longest = l_idx;
		double perim = 0;
		for(int r_idx=(l_idx+1)%npts; ; r_idx=(r_idx+1)%npts) {
			perim += seg_len(pts[(r_idx+npts-1)%npts], pts[r_idx]);
			if(perim > min_length && is_mostly_linear(ring, l_idx, r_idx)) {
				longest = r_idx;
			} else {
				break;
			}
			if(r_idx == to) break;
		}
		if(longest != l_idx) {
			memcpy(keep_new, keep_orig, sizeof(bool) * npts);
			int error = 0;
			if(l_idx != from) {
				if(add_tiepoint(ring, keep_new, l_idx)) error++;
			}
//if(error) fatal_error("oops"); // FIXME
			if(longest != to) {
				if(add_tiepoint(ring, keep_new, longest)) error++;
			}
//if(error) fatal_error("oops"); // FIXME
			if(!error) {
				touchpts[l_idx] = true;
				touchpts[longest] = true;
				for(int i=l_idx; ; i=(i+1)%npts) {
					keep_new[i] = true;
					if(i == longest) break;
				}
				memcpy(keep_orig, keep_new, sizeof(bool) * npts);
				free(keep_new);
				return 1;
			}
		}
	}
	free(keep_new);
	return 0;
}

static int refine_seg(const Ring &ring, bool *keep_orig, int from, int to) {
	int npts = ring.pts.size();
	const std::vector<Vertex> &pts = ring.pts;

	double start_area = subring_area(ring, from, to);
	double start_perim = seg_len(pts[from], pts[to]);

	bool *keep_new = MYALLOC(bool, npts);
	bool *keep_best = MYALLOC(bool, npts);

	double best_improvement = 0;
	int best_touchpt = -1;

	for(int testpt=(from+1)%npts; testpt!=to; testpt=(testpt+1)%npts) {
		memcpy(keep_new, keep_orig, sizeof(bool) * npts);

		if(add_tiepoint(ring, keep_new, testpt)) continue;

		double left_area = 0;
		double left_perim = 0;
		for(int pk=from;;) {
			int nk = next_keep(npts, keep_new, pk);
			left_area += subring_area(ring, pk, nk);
			left_perim += seg_len(pts[pk], pts[nk]);
			if(nk == testpt) break;
			pk = nk;
		}
		double right_area = 0;
		double right_perim = 0;
		for(int pk=testpt;;) {
			int nk = next_keep(npts, keep_new, pk);
			right_area += subring_area(ring, pk, nk);
			right_perim += seg_len(pts[pk], pts[nk]);
			if(nk == to) break;
			pk = nk;
		}
		double area = left_area + right_area;
		double perim = left_perim + right_perim;

		//double improvement = (start_area - area) - 50.0*(perim - start_perim);
		double improvement = ((start_area+2.0) / (area+2.0)) / pow(perim / start_perim, 2) - 2.0;
		//double improvement = (start_area - area) / pow(perim - start_perim, .5) - 100.0;
		//double improvement = (start_area - area) / pow(perim - start_perim, 2) - 100.0;

		/*
		double min_linear = 10;
		if(left_perim > min_linear && left_area /left_perim < 0.55) {
			if(is_mostly_linear(ring, keep_new, from, testpt))
				improvement += left_perim - min_linear;
		}
		if(right_perim > min_linear && right_area/right_perim < 0.55) {
			if(is_mostly_linear(ring, keep_new, testpt, to))
				improvement += right_perim - min_linear;
		}
		*/

		if(VERBOSE >= 2) printf("improvement=%g, start_area=%g, area=%g, perim=%g, start_perim=%g\n",
			improvement, start_area, area, perim, start_perim);

		if(improvement > best_improvement) {
			best_improvement = improvement;
			memcpy(keep_best, keep_new, sizeof(bool) * npts);
			best_touchpt = testpt;
		}
	}
	if(best_improvement) {
		if(VERBOSE) {
			printf("best_improvement = %g\n", best_improvement);
			printf("tagged %d (%g,%g) as keep between %d and %d\n", best_touchpt,
				pts[best_touchpt].x, pts[best_touchpt].y, from, to);
			for(int i=0; i<npts; i++) {
				if(keep_best[i] && !keep_orig[i]) printf(
					"  rubberband touches %d (%g, %g)\n", i, pts[i].x, pts[i].y);
			}
		}

		memcpy(keep_orig, keep_best, sizeof(bool) * npts);
	} else {
		best_touchpt = -1;
	}
	free(keep_new);
	free(keep_best);
	return best_touchpt;
}

static void refine_ring(const Ring &ring, bool *keep, bool *touchpts) {
	int npts = ring.pts.size();
	for(int i=0; i<npts; i++) {
		if(!keep[i]) continue;
		for(;;) {
		// FIXME quick loop if j==i+1
			int j = next_keep(npts, keep, i);
			double area = subring_area(ring, i, j);
			if(VERBOSE) printf("area = %g, refining segment %d,%d\n", area, i, j);
			if(area > 0) {
if(VERBOSE) printf("do linear\n");
				int did_linear = keep_linears(ring, keep, i, j, touchpts);
				if(did_linear) continue;
if(VERBOSE) printf("do refine\n");
				int touchpt = refine_seg(ring, keep, i, j);
				if(touchpt >= 0) {
					touchpts[touchpt] = true;
					continue;
				}
			}
			break;
		}
	}
}

static Ring pinch_ring_excursions(const Ring &ring_in) {
	Ring ring(ring_in);
	int npts = ring.pts.size();

	if(!ring.isCCW()) {
		// reverse ring to make it CCW
		// FIXME! use polygon reverse method
		for(int i=0; i<npts/2; i++) {
			Vertex tmp = ring.pts[i];
			ring.pts[i] = ring.pts[npts-1-i];
			ring.pts[npts-1-i] = tmp;
		}
	}

	const std::vector<Vertex> &pts = ring.pts;

	bool *keep = find_chull(ring);
	bool *touchpts = MYALLOC(bool, ring.pts.size());
	for(int i=0; i<ring.pts.size(); i++) touchpts[i] = false;

	refine_ring(ring, keep, touchpts);

	int nkeep = 0;
	for(int i=0; i<npts; i++) {
		if(keep[i]) nkeep++;
	}

	Ring outring = ring.copyMetadata();
	outring.pts.reserve(nkeep);
	for(int i=0; i<npts; i++) {
		if(keep[i]) outring.pts.push_back(pts[i]);
	}
	assert(outring.pts.size() == nkeep);

//	if(dbuf && dbuf->mode == PLOT_PINCH) {
//		debug_plot_ring(dbuf, &outring, 255, 0, 0);
//		for(int i=0; i<npts; i++) {
//			if(touchpts[i]) {
//				Vertex p = pts[i];
//				plot_point_big(dbuf, p.x, p.y, 255, 255, 255);
//			}
//		}
//	}

	free(keep);
	free(touchpts);

	return outring;
}

static OGRGeometryH ring_to_ogrpoly(const Ring &r) {
	OGRGeometryH ogr = OGR_G_CreateGeometry(wkbPolygon);
	OGR_G_AddGeometry(ogr, ring_to_ogr(r));
	return ogr;
}

// rings must cross for this function
static Ring ring_ring_union(const Ring &r1, const Ring &r2) {
	OGRGeometryH og1 = ring_to_ogrpoly(r1);
	OGRGeometryH og2 = ring_to_ogrpoly(r2);
	OGRGeometryH og3 = OGR_G_Union(og1, og2);
	if(!og3) fatal_error("OGR_G_Union failed");
	OGRwkbGeometryType type = OGR_G_GetGeometryType(og3);
	if(type == wkbPolygon) {
		// only take outer ring
		Ring r3 = ogr_to_ring(OGR_G_GetGeometryRef(og3, 0));
		r3.is_hole = 0;
		r3.parent_id = 0;
		return r3;
	} else {
		fatal_error("result of ring union wasn't a wkbPolygon");
	}
}

Mpoly pinch_excursions2(const Mpoly &mp_in, report_image_t *dbuf) {
	Mpoly mp_out;
	mp_out.rings.resize(mp_in.rings.size());
	for(int r_idx=0; r_idx<mp_in.rings.size(); r_idx++) {
		// FIXME - put a test for this into usage()
		if(mp_in.rings[r_idx].is_hole) fatal_error("pincher cannot be used on holes");
		mp_out.rings[r_idx] = pinch_ring_excursions(mp_in.rings[r_idx]);
	}
	for(int r1_idx=0; r1_idx<mp_out.rings.size(); r1_idx++) {
		REDO_R1:
		if(r1_idx >= mp_out.rings.size()) break;

		for(int r2_idx=r1_idx+1; r2_idx<mp_out.rings.size(); r2_idx++) {
			REDO_R2:
			if(r2_idx >= mp_out.rings.size()) break;

			RingRelation rel = ring_ring_relation(mp_out.rings[r1_idx], mp_out.rings[r2_idx]);
//printf("relation of %d and %d is %d\n", r1_idx, r2_idx, rel);
			if(rel == RINGREL_CONTAINS) {
//printf("deleting %d\n", r2_idx);
				//if(dbuf && dbuf->mode == PLOT_PINCH) {
				//	debug_plot_ring(dbuf, mp_out.rings[r2_idx], 0, 0, 128);
				//}
				mp_out.deleteRing(r2_idx);
				goto REDO_R2; // indexes shifted - reset loop
			} else if(rel == RINGREL_CONTAINED_BY) {
//printf("deleting %d\n", r1_idx);
				//if(dbuf && dbuf->mode == PLOT_PINCH) {
				//	debug_plot_ring(dbuf, mp_out.rings[r1_idx], 0, 0, 128);
				//}
				mp_out.deleteRing(r1_idx);
				goto REDO_R1; // indexes shifted - reset loop
			} else if(rel == RINGREL_CROSSES) {
//printf("merging %d and %d\n", r1_idx, r2_idx);
				Ring r3 = ring_ring_union(mp_out.rings[r1_idx], mp_out.rings[r2_idx]);
				//if(dbuf && dbuf->mode == PLOT_PINCH) {
				//	debug_plot_ring(dbuf, mp_out.rings[r1_idx], 0, 0, 128);
				//	debug_plot_ring(dbuf, mp_out.rings[r2_idx], 0, 0, 128);
				//}
				mp_out.deleteRing(r2_idx);
				mp_out.rings[r1_idx] = r3;
				//if(dbuf && dbuf->mode == PLOT_PINCH) {
				//	debug_plot_ring(dbuf, mp_out.rings[r1_idx], 255, 128, 0);
				//}
				goto REDO_R1; // indexes shifted - reset loop
			}
		}
	}

	if(dbuf && dbuf->mode == PLOT_PINCH) {
		for(int i=0; i<mp_out.rings.size(); i++) {
			debug_plot_ring(dbuf, mp_out.rings[i], 255, 0, 0);
		}
	}

	// FIXME - fix topology using functions from dp.c
	return mp_out;
}

} // namespace dangdal
