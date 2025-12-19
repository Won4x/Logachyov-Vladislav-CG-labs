// Buffer B: состояние снаряда

const float BULLET_LIFETIME = 4.0;

bool keyDownByTexture(int ascii) {
    float x = (float(ascii) + 0.5) / 256.0;
    float v = texture(iChannel3, vec2(x, (0.5/3.0))).x;
    return v > 0.5;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec4 prev = texture(iChannel1, vec2(0.5));
    float prevActive = prev.x;
    float prevStart  = prev.y;
    float prevDirYaw = prev.z;
    float prevDirPitch = prev.w;

    // текущая турель из Buffer A
    vec3 turretState = texture(iChannel0, vec2(0.5)).rgb;
    float turretYaw   = turretState.r;
    float turretPitch = turretState.g;

    float now = iTime;

    // кнопка стрельбы (пробел)
    float shoot = keyDownByTexture(32) ? 1.0 : 0.0;

    float outActive = prevActive;
    float outStart  = prevStart;
    float outDirYaw = prevDirYaw;
    float outDirPitch = prevDirPitch;

    if (shoot > 0.5 && prevActive < 0.5) {
        outActive = 1.0;
        outStart = now;
        outDirYaw = turretYaw;
        outDirPitch = turretPitch;
    }

    // если снаряд активен и время жизни вышло — деактивируем
    if (outActive > 0.5 && (now - outStart) > BULLET_LIFETIME) {
        outActive = 0.0;
    }

    fragColor = vec4(outActive, outStart, outDirYaw, outDirPitch);
}
