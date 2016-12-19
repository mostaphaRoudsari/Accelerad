/*
 * Copyright (c) 2013-2016 Nathaniel Jones
 * Massachusetts Institute of Technology
 */

#include <optix.h>
#include <optixu/optixu_math_namespace.h>
#include "optix_shader_common.h"
#include "optiX_shader_ray.h"
#ifdef CONTRIB
#include "optix_shader_contrib.h"
#endif

using namespace optix;

/* Context variables */
rtDeclareVariable(rtObject,     top_object, , );

/* Material variables */
#ifdef HIT_TYPE
rtDeclareVariable(unsigned int, type, , ); /* The material type representing "glass" or "dielectric" */
#endif
rtDeclareVariable(float,        r_index, , ) = 1.52f; /* Refractive index, usually 1.52 */
rtDeclareVariable(float3,       color, , ); /* The material color given by the rad file "glass" object */

/* OptiX variables */
rtDeclareVariable(Ray, ray, rtCurrentRay, );
rtDeclareVariable(float, t_hit, rtIntersectionDistance, );
rtDeclareVariable(PerRayData_radiance, prd, rtPayload, );
rtDeclareVariable(PerRayData_shadow,   prd_shadow, rtPayload, );

/* Attributes */
rtDeclareVariable(float3, geometric_normal, attribute geometric_normal, );
rtDeclareVariable(float3, shading_normal, attribute shading_normal, );
#ifdef ANTIMATTER
rtDeclareVariable(int, mat_id, attribute mat_id, );
#endif


RT_PROGRAM void closest_hit_shadow()
{
	float3 world_shading_normal   = normalize( rtTransformNormal( RT_OBJECT_TO_WORLD, shading_normal ) );
	float3 world_geometric_normal = normalize( rtTransformNormal( RT_OBJECT_TO_WORLD, geometric_normal ) );

	float3 ffnormal = faceforward( world_shading_normal, -ray.direction, world_geometric_normal );
	float3 snormal = faceforward( world_geometric_normal, -ray.direction, world_geometric_normal );

	float3 hit_point = ray.origin + t_hit * ray.direction;
	float3 mcolor = color;

#ifdef ANTIMATTER
	if (prd_shadow.mask & (1 << mat_id)) {
		prd_shadow.inside += dot(world_geometric_normal, ray.direction) < 0.0f ? 1 : -1;

		/* Continue the ray */
		Ray new_ray = make_Ray(ray.origin, ray.direction, ray.ray_type, ray_start(hit_point, ray.direction, snormal, RAY_START) + t_hit, RAY_END);
		rtTrace(top_object, new_ray, prd_shadow);
		return;
	}
#endif /* ANTIMATTER */

	/* check transmission */
	bool hastrans = fmaxf( mcolor ) > 1e-15f;
	if (!hastrans) {
		return;
	}
	mcolor = fmaxf( mcolor, make_float3( 1e-15f ) ); // no color channel should be smaller than 1e-15

	/* get modifiers */
	// we'll skip this for now

	/* perturb normal */
	// if there's a bump map, we use that, else
	float pdot = -dot( ray.direction, ffnormal );
	if (pdot < 0.0f) {		/* fix orientation from raynormal in raytrace.c */
		ffnormal += 2.0f * pdot * ray.direction;
		pdot = -pdot;
	}

	/* angular transmission */
	float cos2 = sqrtf(1.0f + (pdot * pdot - 1.0f) / (r_index * r_index));
	//if (hastrans) {
	mcolor = make_float3( powf( mcolor.x, 1.0f / cos2 ), powf( mcolor.y, 1.0f / cos2 ), powf( mcolor.z, 1.0f / cos2 ) );
	//}

	/* compute reflection */
	float r1e = (pdot - r_index * cos2) / (pdot + r_index * cos2);
	r1e *= r1e;
	float r1m = (1.0f / pdot - r_index / cos2) / (1.0f / pdot + r_index / cos2);
	r1m *= r1m;

	/* compute transmission */
	//if (hastrans) {
		float3 trans = 0.5f * (1.0f-r1e) * (1.0f-r1e) * mcolor / (1.0f - r1e * r1e * mcolor * mcolor);
		trans       += 0.5f * (1.0f-r1m) * (1.0f-r1m) * mcolor / (1.0f - r1m * r1m * mcolor * mcolor);

		/* modify by pattern */
		//trans *= pcol;

		/* transmitted ray */
#ifdef CONTRIB
		prd_shadow.rcoef *= trans;
#endif
		Ray trans_ray = make_Ray(hit_point, ray.direction, ray.ray_type, ray_start(hit_point, ray.direction, snormal, RAY_START), RAY_END);
		rtTrace(top_object, trans_ray, prd_shadow);
		prd_shadow.result *= trans;
#ifdef DAYSIM_COMPATIBLE
		daysimScale(prd_shadow.dc, trans.x);
#endif
	//}
}


RT_PROGRAM void closest_hit_radiance()
{
	float3 world_shading_normal   = normalize( rtTransformNormal( RT_OBJECT_TO_WORLD, shading_normal ) );
	float3 world_geometric_normal = normalize( rtTransformNormal( RT_OBJECT_TO_WORLD, geometric_normal ) );

	float3 ffnormal = faceforward( world_shading_normal, -ray.direction, world_geometric_normal );
	float3 snormal = faceforward( world_geometric_normal, -ray.direction, world_geometric_normal );

	PerRayData_radiance new_prd;             
	float3 result = make_float3( 0.0f );
	float3 hit_point = ray.origin + t_hit * ray.direction;
	float3 mcolor = color;

#ifdef ANTIMATTER
	if (prd.mask & (1 << mat_id)) {
		prd.inside += dot(world_geometric_normal, ray.direction) < 0.0f ? 1 : -1;

		/* Continue the ray */
		Ray new_ray = make_Ray(ray.origin, ray.direction, ray.ray_type, ray_start(hit_point, ray.direction, snormal, RAY_START) + t_hit, RAY_END);
		rtTrace(top_object, new_ray, prd);
		return;
	}
#endif /* ANTIMATTER */

	/* check transmission */
	bool hastrans = fmaxf( mcolor ) > 1e-15f;
	if (hastrans) {
		mcolor = fmaxf( mcolor, make_float3( 1e-15f ) ); // no color channel should be smaller than 1e-15
	} // else we return if it's a shadow ray, which it isn't

	/* get modifiers */
	// we'll skip this for now

	float transtest = 0.0f, transdist = t_hit;
	float mirtest = 0.0f, mirdist = t_hit;

	/* perturb normal */
	float3 pert = snormal - ffnormal;
	int hastexture = dot(pert, pert) > FTINY * FTINY;
	float pdot = -dot(ray.direction, ffnormal);
	if (pdot < 0.0f) {		/* fix orientation from raynormal in raytrace.c */
		ffnormal += 2.0f * pdot * ray.direction;
		pdot = -pdot;
	}

	/* angular transmission */
	float cos2 = sqrtf(1.0f + (pdot * pdot - 1.0f) / (r_index * r_index));
	if (hastrans) {
		mcolor = make_float3( powf( mcolor.x, 1.0f / cos2 ), powf( mcolor.y, 1.0f / cos2 ), powf( mcolor.z, 1.0f / cos2 ) );
	}

	/* compute reflection */
	float r1e = (pdot - r_index * cos2) / (pdot + r_index * cos2);
	r1e *= r1e;
	float r1m = (1.0f / pdot - r_index / cos2) / (1.0f / pdot + r_index / cos2);
	r1m *= r1m;

	/* compute transmission */
	if (hastrans) {
		float3 trans = 0.5f * (1.0f-r1e) * (1.0f-r1e) * mcolor / (1.0f - r1e * r1e * mcolor * mcolor);
		trans       += 0.5f * (1.0f-r1m) * (1.0f-r1m) * mcolor / (1.0f - r1m * r1m * mcolor * mcolor);

		/* modify by pattern */
		//trans *= pcol;

		/* transmitted ray */
		if (rayorigin(new_prd, prd, trans, 0, 0)) {
#ifdef DAYSIM_COMPATIBLE
			new_prd.dc = daysimNext(prd.dc);
#endif
			float3 R = ray.direction;

			if (!new_prd.ambient_depth && hastexture) {
				R = normalize(ray.direction + pert * (2.0f * (1.0f - r_index)));
				if (isnan(R))
					R = ray.direction;
			} else {
				transtest = 2;
			}

			setupPayload(new_prd);
			Ray trans_ray = make_Ray(hit_point, R, ray.ray_type, ray_start(hit_point, R, snormal, RAY_START), RAY_END);
			rtTrace(top_object, trans_ray, new_prd);
			float3 rcol = new_prd.result * trans;
			result += rcol;
			transtest *= bright( rcol );
			transdist = t_hit + new_prd.distance;
#ifdef DAYSIM_COMPATIBLE
			daysimAddScaled(prd.dc, new_prd.dc, trans.x);
#endif
			resolvePayload(prd, new_prd);
		}
	}
	// stop if it's a shadow ray, which it isn't

	/* compute reflectance */
	float3 refl = 0.5f * r1e * ( 1.0f + (1.0f-2.0f*r1e) * mcolor * mcolor ) / (1.0f - r1e * r1e * mcolor * mcolor );
	refl       += 0.5f * r1m * ( 1.0f + (1.0f-2.0f*r1m) * mcolor * mcolor ) / (1.0f - r1m * r1m * mcolor * mcolor );

	/* reflected ray */
	if (rayorigin(new_prd, prd, refl, 1, 0)) {
#ifdef DAYSIM_COMPATIBLE
		new_prd.dc = daysimNext(prd.dc);
#endif
		setupPayload(new_prd);
		float3 R = reflect( ray.direction, ffnormal );
		Ray refl_ray = make_Ray(hit_point, R, ray.ray_type, ray_start(hit_point, R, snormal, RAY_START), RAY_END);
		rtTrace(top_object, refl_ray, new_prd);
		float3 rcol = new_prd.result * refl;
		result += rcol;
		if (prd.ambient_depth || !hastexture) {
			mirtest = 2.0f * bright(rcol);
			mirdist = t_hit + new_prd.distance;
		}
#ifdef DAYSIM_COMPATIBLE
		daysimAddScaled(prd.dc, new_prd.dc, refl.x);
#endif
		resolvePayload(prd, new_prd);
	}
  
	/* check distance */
	float d = bright( result );
	if (transtest > d)
		prd.distance = transdist;
	else if (mirtest > d)
		prd.distance = mirdist;
	else
		prd.distance = t_hit;

	// pass the color back up the tree
	prd.result = result;
	
#ifdef HIT_TYPE
	prd.hit_type = type;
#endif
#ifdef CONTRIB
	contribution(prd.rcoef, result, ray.direction);
#endif
}