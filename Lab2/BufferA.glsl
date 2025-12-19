// Buffer A: состояние турели

const float TURN_SPEED  = -0.2;
const float PITCH_SPEED = 0.1;
const float MAX_DT      = 0.1;
const float BULLET_LIFETIME = 1.0;

bool keyDownByTexture(int ascii) {
    float x = (float(ascii) + 0.5) / 256.0;
    float v = texture(iChannel3, vec2(x, (0.5/3.0))).x;
    return v > 0.5;
}

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    // читаем предыдущее состояние
    vec4 prev = texture(iChannel0, vec2(0.5));
    float prevYaw   = prev.r;
    float prevPitch = prev.g;
    float prevStart = prev.b;
    float prevActive = prev.a;

    float now = iTime;
    float dt = clamp(now - prevStart, 0.0, MAX_DT);

    // читаем клавиши
    float left  = keyDownByTexture(65) ? 1.0 : 0.0; // 'A'
    float right = keyDownByTexture(68) ? 1.0 : 0.0; // 'D'
    float up    = keyDownByTexture(87) ? 1.0 : 0.0; // 'W'
    float down  = keyDownByTexture(83) ? 1.0 : 0.0; // 'S'
    float shoot = keyDownByTexture(32) ? 1.0 : 0.0; // пробел

    float dt2 = clamp(iTime - prev.b, 0.0, MAX_DT);

    float yawDelta   = (right - left) * TURN_SPEED * dt2;
    float pitchDelta = (up - down) * PITCH_SPEED * dt2;

    const float PI = 3.141592653589793;
    float newYaw = prevYaw + yawDelta;
    if (newYaw > PI) newYaw -= 2.0 * PI;
    if (newYaw < -PI) newYaw += 2.0 * PI;
    float newPitch = clamp(prevPitch + pitchDelta, -0.7, 0.7);

    float outStart = prevStart;
    float outActive = prevActive;

    if (shoot > 0.5 && prevActive < 0.5) {
        outActive = 1.0;
        outStart = now;
    }

    if (outActive > 0.5 && (now - outStart) > BULLET_LIFETIME) {
        outActive = 0.0;
        
    }

    fragColor = vec4(newYaw, newPitch, outStart, outActive);
}
