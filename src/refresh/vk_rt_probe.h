/* One-shot CPU reflection-path verification. Included only by vk_backend.c,
 * after the alias pose helpers, with USE_VULKAN_RAYTRACING enabled.
 * This deliberately uses BSP polygons independently of the GPU hit table.
 */
static bool vk_rt_probe_pending;
static float vk_rt_probe_pixel[2];
static bool vk_rt_probe_center;

static void vk_rt_probe_f(void)
{
    if (Cmd_Argc() != 1 && Cmd_Argc() != 3) {
        Com_Printf("Usage: rtprobe [screen_x screen_y] (otherwise crosshair)\n");
        return;
    }
    vk_rt_probe_center = Cmd_Argc() == 1;
    if (!vk_rt_probe_center) {
        vk_rt_probe_pixel[0] = atof(Cmd_Argv(1));
        vk_rt_probe_pixel[1] = atof(Cmd_Argv(2));
        if (!isfinite(vk_rt_probe_pixel[0]) || !isfinite(vk_rt_probe_pixel[1]))
            return;
    }
    vk_rt_probe_pending = true;
}

static float vk_rt_probe_triangle(const vec3_t origin, const vec3_t direction,
                                 const vec3_t a, const vec3_t b, const vec3_t c)
{
    vec3_t e1, e2, p, t, q;
    VectorSubtract(b, a, e1);
    VectorSubtract(c, a, e2);
    CrossProduct(direction, e2, p);
    float det = DotProduct(e1, p);
    if (fabsf(det) < 0.00000001f)
        return -1.0f;
    VectorSubtract(origin, a, t);
    float u = DotProduct(t, p) / det;
    if (u < -0.00001f || u > 1.00001f)
        return -1.0f;
    CrossProduct(t, e1, q);
    float v = DotProduct(direction, q) / det;
    if (v < -0.00001f || u + v > 1.00001f)
        return -1.0f;
    float distance = DotProduct(e2, q) / det;
    return distance >= 0.15f ? distance : -1.0f;
}

static const vec_t *vk_rt_probe_face_vertex(const bsp_t *bsp,
                                          const mface_t *face, int index)
{
    const msurfedge_t *se = &face->firstsurfedge[index];
    return bsp->vertices[bsp->edges[se->edge].v[se->vert]].point;
}

static const mface_t *vk_rt_probe_world(const vec3_t origin,
                                      const vec3_t direction,
                                      float *nearest, vec3_t normal)
{
    const bsp_t *bsp = vk.world.cache;
    const mface_t *hit = NULL;
    for (int i = 0; i < bsp->numfaces; i++) {
        const mface_t *face = &bsp->faces[i];
        if (face->numsurfedges < 3 ||
            (face->drawflags & (SURF_TRANS_MASK | SURF_SKY | SURF_NODRAW)) ||
            vk_world_face_is_inline(bsp, face))
            continue;
        vec3_t n;
        VectorCopy(face->plane->normal, n);
        if (face->drawflags & DSURF_PLANEBACK)
            VectorNegate(n, n);
        if (DotProduct(n, direction) >= -0.00001f)
            continue;
        const vec_t *a = vk_rt_probe_face_vertex(bsp, face, 0);
        for (int j = 1; j + 1 < face->numsurfedges; j++) {
            float distance = vk_rt_probe_triangle(origin, direction, a,
                vk_rt_probe_face_vertex(bsp, face, j),
                vk_rt_probe_face_vertex(bsp, face, j + 1));
            if (distance > 0.0f && distance < *nearest) {
                *nearest = distance;
                hit = face;
                VectorCopy(n, normal);
            }
        }
    }
    return hit;
}

static void vk_rt_probe_frame(const refdef_t *fd)
{
    if (!vk_rt_probe_pending)
        return;
    vk_rt_probe_pending = false;
    if (!vk.world.cache || !vk.raytracing_active ||
        (fd->rdflags & RDF_NOWORLDMODEL) || fd->width <= 0 || fd->height <= 0) {
        Com_Printf("RT probe: no active world RT scene\n");
        return;
    }
    float x = vk_rt_probe_center ? fd->x + fd->width * 0.5f : vk_rt_probe_pixel[0];
    float y = vk_rt_probe_center ? fd->y + fd->height * 0.5f : vk_rt_probe_pixel[1];
    if (x < fd->x || x >= fd->x + fd->width ||
        y < fd->y || y >= fd->y + fd->height) {
        Com_Printf("RT probe: pixel outside the view\n");
        return;
    }
    vec3_t axis[3], direction, normal, point, origin;
    AnglesToAxis(fd->viewangles, axis);
    VectorCopy(axis[0], direction);
    float sx = (2.0f * (x - fd->x) / fd->width - 1.0f) *
        tanf(fd->fov_x * (M_PI / 360.0f));
    float sy = (2.0f * (y - fd->y) / fd->height - 1.0f) *
        tanf(fd->fov_y * (M_PI / 360.0f));
    VectorMA(direction, -sx, axis[1], direction);
    VectorMA(direction, -sy, axis[2], direction);
    VectorNormalize(direction);
    float camera_distance = 8192.0f;
    const mface_t *receiver = vk_rt_probe_world(fd->vieworg, direction,
                                                &camera_distance, normal);
    if (!receiver) {
        Com_Printf("RT probe: no static BSP receiver at %.1f %.1f\n", x, y);
        return;
    }
    VectorMA(fd->vieworg, camera_distance, direction, point);
    VectorMA(direction, -2.0f * DotProduct(direction, normal), normal, direction);
    float magnitude = max(fabsf(point[0]), max(fabsf(point[1]), fabsf(point[2])));
    float bias = min(0.75f, max(0.25f, 0.25f + magnitude * 0.00005f));
    VectorMA(point, bias, normal, origin);
    Com_Printf("RT probe: pixel %.1f %.1f receiver=%s face=%d point=(%.3f %.3f %.3f)\n",
        x, y, receiver->texinfo->name, (int)(receiver - vk.world.cache->faces),
        point[0], point[1], point[2]);
    Com_Printf("RT probe: origin=(%.3f %.3f %.3f) direction=(%.6f %.6f %.6f)\n",
        origin[0], origin[1], origin[2], direction[0], direction[1], direction[2]);
    float world_distance = 1536.0f;
    const mface_t *world = vk_rt_probe_world(origin, direction, &world_distance, normal);
    float model_distance = 1536.0f;
    char model_name[MAX_QPATH] = { 0 };
    int entity_index = -1;
    for (int i = 0; vk_rt_models && vk_rt_models->integer && i < fd->num_entities; i++) {
        const entity_t *ent = &fd->entities[i];
        vk_model_t view;
        vk_model_t *model = vk_rt_entity_geometry(ent, fd, &view);
        if (!model || model->type != VK_MODEL_ALIAS ||
            !model->rt_vertices || !model->rt_indices ||
            (ent->flags & (RF_WEAPONMODEL | RF_TRANSLUCENT | RF_BLOOM_ONLY)))
            continue;
        vk_alias_lerp_t lerp = vk_alias_lerp_for_entity(model, ent, fd);
        vec3_t model_axis[3];
        vk_entity_axis(ent, model_axis);
        for (uint32_t j = 0; j + 2 < model->mesh.index_count; j += 3) {
            vec3_t p[3];
            for (int corner = 0; corner < 3; corner++) {
                uint32_t index = model->rt_indices[j + corner];
                const vk_vertex_t *current = &model->rt_vertices[
                    (size_t)lerp.frame * model->vertex_count + index];
                const vk_vertex_t *old = &model->rt_vertices[
                    (size_t)lerp.oldframe * model->vertex_count + index];
                VectorCopy(ent->origin, p[corner]);
                for (int c = 0; c < 3; c++) {
                    float position = current->position[c] * (1.0f - lerp.backlerp) +
                                     old->position[c] * lerp.backlerp;
                    VectorMA(p[corner], position, model_axis[c], p[corner]);
                }
            }
            float distance = vk_rt_probe_triangle(origin, direction, p[0], p[1], p[2]);
            if (distance > 0.0f && distance < model_distance) {
                model_distance = distance;
                Q_strlcpy(model_name, model->name, sizeof(model_name));
                entity_index = i;
            }
        }
    }
    if (world)
        Com_Printf("RT probe: nearest world=%s face=%d distance=%.3f\n",
            world->texinfo->name, (int)(world - vk.world.cache->faces), world_distance);
    else
        Com_Printf("RT probe: no world hit within 1536 units\n");
    if (model_name[0])
        Com_Printf("RT probe: nearest model=%s entity=%d distance=%.3f (%s)\n",
            model_name, entity_index, model_distance,
            model_distance < world_distance ? "visible: clears static BSP" : "blocked by static BSP");
    else
        Com_Printf("RT probe: no model hit within 1536 units\n");
    Com_Printf("RT probe: inline brush entities are not covered by this check\n");
}
