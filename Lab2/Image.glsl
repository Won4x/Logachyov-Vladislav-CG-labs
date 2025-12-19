// Цвета материалов (RGB [0..1])
const vec3 WATER_BASE_COLOR = vec3(0.02, 0.08, 0.12);
const vec3 SHIP1_COLOR      = vec3(0.75, 0.25, 0.12);
const vec3 SHIP2_COLOR      = vec3(0.14, 0.32, 0.70);

// Параметры волн (вес каждого слоя, частота, скорость)
const float WAVE_A0 = 0.25;
const float WAVE_F0 = 2.5;
const float WAVE_S0 = 1.3;

const float WAVE_A1 = 0.18;
const float WAVE_F1 = 1.2;
const float WAVE_S1 = 1.35;

const float WAVE_A2 = 0.08;
const float WAVE_F2 = 2.2;
const float WAVE_S2 = 2.1;

const float WAVE_A3 = 0.05;
const float WAVE_F3 = 1.0;
const float WAVE_S3 = 5.0;

const float GLOBAL_WAVE_SCALE = 0.3;   // общий множитель амплитуды

// Параметры камеры
const vec3 CAM_POS    = vec3(0.0, 1.7, 0.5);
const vec3 CAM_LOOKAT = vec3(0.0, 0.2, 4.5);
const float CAM_FOV   = 1.05;

// Параметры трассировки луча
const float MAX_TRACE_DIST = 120.0;
const int   MAX_STEPS      = 140;
const float SURF_EPS       = 0.0009;


// ---------- Примитивы SDF ----------
float sdSphere(vec3 p, float r) { return length(p) - r; }

float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}


// ---------- Базовые вращения ----------
mat2 rot(float a) {
    float c = cos(a), s = sin(a);
    return mat2(c, -s, s, c);
}

// Вращение вокруг произвольной точки (pivot) по оси axis
vec3 rotateAroundPoint(vec3 p, vec3 pivot, vec3 axis, float angle) {
    axis = normalize(axis);
    vec3 rel = p - pivot;

    float c = cos(angle);
    float s = sin(angle);
    vec3 rotated = rel * c + cross(axis, rel) * s + axis * dot(axis, rel) * (1.0 - c);

    return pivot + rotated;
}


// ---------- Высота волн ----------
float waveHeight(vec2 xz) {
    float t = iTime;
    float h = 0.0;
    h += WAVE_A0 * sin(WAVE_F0 * xz.x + WAVE_S0 * t);
    h += WAVE_A1 * sin(WAVE_F1 * xz.y - WAVE_S1 * t + 0.5 * xz.x);
    h += WAVE_A2 * sin(WAVE_F2 * xz.x + WAVE_F2 * xz.y + WAVE_S2 * t);
    h += WAVE_A3 * sin(WAVE_F3 * xz.x + WAVE_F3 * xz.y + WAVE_S3 * t);
    return h * GLOBAL_WAVE_SCALE;
}


// ---------- Вода как height-field ----------
float sdWater(vec3 p) {
    float h = waveHeight(p.xz);
    return p.y - h;
}


// ---------- Объединение SDF ----------
vec2 opUnion(vec2 a, vec2 b) { return (a.x < b.x) ? a : b; }


// ---------- Сцена ----------
vec2 map(vec3 p) {
    vec2 res = vec2(1e5, -1.0);

    vec3 turretState = texture(iChannel0, vec2(0.5)).rgb;
    float turretYaw   = turretState.r;
    float turretPitch = turretState.g;

    // ===== ВОДА =====
    float w = sdWater(p);
    res = opUnion(res, vec2(w, 0.0));

    // ===== КОРАБЛЬ 1 =====
    const vec3 TURRET_PIVOT = vec3(0.0, 0.5, 0.45);
    const vec3 GUN_CENTER   = vec3(0.0, 0.5, 0.55);
    const vec3 GUN_LEFT     = vec3(-0.15, 0.5, 0.55);
    const vec3 GUN_RIGHT    = vec3( 0.15, 0.5, 0.55);
    {
        vec3 s = p - vec3(0.0, 0.1, 3.0);
        s.xz *= rot(iTime * 0.0);

        // Корпус
        float hullFront = sdBox(s - vec3(0.0, -0.1, 0.5), vec3(0.6, 0.2, 0.8));
        float hullBack  = sdBox(s - vec3(0.0, 0.0, -0.3), vec3(0.8, 0.2, 1.2));
        float hull = mix(hullFront, hullBack, clamp((s.z + 1.0) * 0.5, 0.0, 1.0));
        float keel = sdBox(s - vec3(0.0, -0.15, 0.0), vec3(0.5, 0.05, 1.1));
        hull = min(hull, keel);

        // Рубка
        float cabin = sdBox(s - vec3(0.0, 0.25, 0.2), vec3(0.35, 0.25, 0.4));

        // Параметры турелей
        const vec3 TURRET_PIVOT = vec3(0.0, 0.5, 0.45);
        const vec3 GUN_CENTER   = vec3(0.0, 0.5, 0.55);
        const vec3 GUN_LEFT     = vec3(-0.15, 0.5, 0.55);
        const vec3 GUN_RIGHT    = vec3( 0.15, 0.5, 0.55);

        // Турель
        vec3 turRot = rotateAroundPoint(s, TURRET_PIVOT, vec3(0.0,1.0,0.0), turretYaw);
        float turret = sdSphere(turRot - TURRET_PIVOT, 0.10);

        // Центральная пушка
        vec3 gunBaseC = rotateAroundPoint(s, TURRET_PIVOT, vec3(0.0,1.0,0.0), turretYaw);
        vec3 gunRotC  = rotateAroundPoint(gunBaseC, GUN_CENTER, vec3(1.0,0.0,0.0), turretPitch);
        float gunC = sdBox(gunRotC - GUN_CENTER, vec3(0.05,0.03,0.25));

        // Левая пушка
        vec3 gunBaseL = rotateAroundPoint(s, TURRET_PIVOT, vec3(0.0,1.0,0.0), turretYaw);
        vec3 gunRotL  = rotateAroundPoint(gunBaseL, GUN_LEFT, vec3(1.0,0.0,0.0), turretPitch);
        float gunL = sdBox(gunRotL - GUN_LEFT, vec3(0.05,0.03,0.25));

        // Правая пушка
        vec3 gunBaseR = rotateAroundPoint(s, TURRET_PIVOT, vec3(0.0,1.0,0.0), turretYaw);
        vec3 gunRotR  = rotateAroundPoint(gunBaseR, GUN_RIGHT, vec3(1.0,0.0,0.0), turretPitch);
        float gunR = sdBox(gunRotR - GUN_RIGHT, vec3(0.05,0.03,0.25));

        // Объединяем все части
        float ship1 = min(min(hull, cabin), min(turret, min(gunC, min(gunL, gunR))));
        res = opUnion(res, vec2(ship1, 1.0));
    }

    // ===== КОРАБЛЬ 2 =====
    {
        vec3 s = p - vec3(1.8, 0.1, 15.0);
        s.xz *= rot(-iTime * 0.08);

        float hullFront = sdBox(s - vec3(0.0, -0.1, 0.5), vec3(0.55, 0.2, 0.7));
        float hullBack  = sdBox(s - vec3(0.0, 0.0, -0.3), vec3(0.75, 0.2, 1.1));
        float hull = mix(hullFront, hullBack, clamp((s.z + 1.0) * 0.5, 0.0, 1.0));
        float keel = sdBox(s - vec3(0.0, -0.15, 0.0), vec3(0.45, 0.05, 1.0));
        hull = min(hull, keel);

        float cabin = sdBox(s - vec3(0.0, 0.25, 0.0), vec3(0.3, 0.25, 0.35));
        float turret = sdSphere(s - vec3(0.0, 0.5, 0.15), 0.10);
        float gun = sdBox(s - vec3(0.0, 0.5, 0.35), vec3(0.04, 0.03, 0.25));

        float ship2 = min(min(hull, cabin), min(turret, gun));
        res = opUnion(res, vec2(ship2, 2.0));
    }
    
    // ===== СНАРЯД =====
    for (int i = 0; i < 3; i++) {
        vec4 bstate = texture(iChannel1, vec2(float(i)/3.0 + 0.1667)); // 0.1667 = 0.5/3.0, середина сегмента
        float bulletActive = bstate.x;
        float startTime    = bstate.y;
        float bulletYaw    = bstate.z;
        float bulletPitch  = bstate.w;

        if (bulletActive > 0.5) {
            float t = iTime - startTime;
            const float BULLET_SPEED = 10.0;

            vec3 gunPivot = (i == 0) ? GUN_CENTER : (i == 1) ? GUN_LEFT : GUN_RIGHT;
            gunPivot += vec3(0.0, 0.1, 3.0);
            vec3 dir;
            dir.x = -sin(bulletYaw) * cos(bulletPitch);
            dir.y = sin(bulletPitch);
            dir.z =  cos(bulletYaw) * cos(bulletPitch);

            vec3 bulletPos = gunPivot + dir * (t * BULLET_SPEED);
            float bullet = sdSphere(p - bulletPos, 0.05);

            res = opUnion(res, vec2(bullet, 3.0));
        }
    }


    
    return res;
}


// ---------- Получение нормали ----------
vec3 getNormal(vec3 p) {
    float eps = 0.0009;
    vec2 e = vec2(1.0, -1.0) * 0.5773;
    float d1 = map(p + e.xyy * eps).x;
    float d2 = map(p + e.yyx * eps).x;
    float d3 = map(p + e.yxy * eps).x;
    float d4 = map(p + e.xxx * eps).x;
    return normalize(e.xyy * d1 + e.yyx * d2 + e.yxy * d3 + e.xxx * d4);
}


// ---------- Ray marching ----------
vec2 rayMarch(vec3 ro, vec3 rd) {
    float t = 0.0;
    for (int i = 0; i < MAX_STEPS; i++) {
        vec3 p = ro + rd * t;
        vec2 m = map(p);
        float d = m.x;
        if (d < SURF_EPS) {
            return vec2(t, m.y);
        }
        t += max(0.001, d);
        if (t > MAX_TRACE_DIST) break;
    }
    return vec2(-1.0, -1.0);
}


// ---------- Цвет по материалу ----------
vec3 materialColor(float id) {
    if (abs(id - 0.0) < 0.1) return WATER_BASE_COLOR;
    if (abs(id - 1.0) < 0.1) return SHIP1_COLOR;
    if (abs(id - 2.0) < 0.1) return SHIP2_COLOR;
    if (abs(id - 3.0) < 0.1) return vec3(0.9, 0.85, 0.2);
    return vec3(0.8);
}


// ---------- Отражения для воды ----------
vec3 sampleSceneColor(vec3 ro, vec3 rd) {
    vec2 h = rayMarch(ro, rd);
    if (h.x <= 0.0) {
        return vec3(0.47, 0.68, 0.9);
    }
    vec3 p = ro + rd * h.x;
    float mid = h.y;
    if (abs(mid - 0.0) < 0.1) {
        return vec3(0.47, 0.68, 0.9);
    }
    vec3 n = getNormal(p);
    vec3 light = normalize(vec3(5.0, 8.0, -2.0) - p);
    float diff = clamp(dot(n, light), 0.0, 1.0);
    vec3 base = materialColor(mid);
    return base * (0.12 + 0.9 * diff);
}


// ---------- Шейдинг воды ----------
vec3 shadeWater(vec3 p, vec3 ro, vec3 rd, vec3 n) {
    vec3 V = normalize(ro - p);
    float fresnel = pow(1.0 - max(dot(V, n), 0.0), 3.0) * 0.8;
    vec3 R = reflect(rd, n);

    vec3 ro_ref = p + n * 0.02 + R * 0.01;
    vec3 refl = sampleSceneColor(ro_ref, R);

    float spec = pow(max(dot(reflect(-normalize(vec3(0.4,0.6,0.3)), n), V), 0.0), 40.0);
    vec3 base = materialColor(0.0);
    float depthFactor = clamp(( -p.y ) * 0.3, 0.0, 1.0);
    vec3 shallow = vec3(0.02, 0.09, 0.12);
    vec3 deep = vec3(0.01, 0.03, 0.06);
    vec3 subsurface = mix(shallow, deep, depthFactor);

    vec3 col = mix(subsurface + base * 0.2, refl, fresnel);
    col += spec * vec3(1.0, 0.9, 0.75);

    float crest = smoothstep(0.0, 0.18, 1.0 - n.y) * 0.25;
    col = mix(col, col + vec3(0.12,0.12,0.12), crest);

    float fog = clamp(length(p - ro) * 0.02, 0.0, 1.0);
    vec3 sky = vec3(0.47, 0.68, 0.9);
    col = mix(col, sky, fog * 0.6);

    return clamp(col, 0.0, 1.0);
}


// ---------- Шейдинг объектов ----------
vec3 shadeObject(vec3 p, vec3 ro, float matID) {
    vec3 n = getNormal(p);
    vec3 light = normalize(vec3(5.0, 8.0, -2.0) - p);
    float diff = max(dot(n, light), 0.0);
    vec3 base = materialColor(matID);
    vec3 col = base * (0.12 + 0.95 * diff);
    float rim = pow(1.0 - max(dot(normalize(ro - p), n), 0.0), 3.0);
    col += rim * 0.06;
    float fog = clamp(length(p - ro) * 0.02, 0.0, 1.0);
    vec3 sky = vec3(0.47, 0.68, 0.9);
    col = mix(col, sky, fog * 0.5);
    return col;
}


// ---------- Камера ----------
vec3 getRayDir(vec2 uv, vec3 ro, vec3 lookAt) {
    vec3 forward = normalize(lookAt - ro);
    vec3 right = normalize(cross(vec3(0.0,1.0,0.0), forward));
    vec3 up = cross(forward, right);
    return normalize(forward + uv.x * right * CAM_FOV + uv.y * up * CAM_FOV);
}


// ---------- Основная функция ----------
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    vec3 ro = CAM_POS;
    vec3 lookAt = CAM_LOOKAT;
    vec3 rd = getRayDir(uv, ro, lookAt);

    vec2 hit = rayMarch(ro, rd);
    vec3 col;

    if (hit.x > 0.0) {
        vec3 p = ro + rd * hit.x;
        float mid = hit.y;

        if (abs(mid - 0.0) < 0.1) {
            vec3 n = getNormal(p);
            col = shadeWater(p, ro, rd, n);
        } else {
            col = shadeObject(p, ro, mid);
        }
    } else {
        col = vec3(0.6, 0.8, 1.0);
    }

    fragColor = vec4(col, 1.0);
}
