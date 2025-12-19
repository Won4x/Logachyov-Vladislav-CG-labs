// BufferA
// iChannel0 = BufferA (self)
// iChannel1 = Keyboard

void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    // Читаем прошлое состояние (смещение по X хранится в красном канале)
    vec4 prev = texelFetch(iChannel0, ivec2(0,0), 0);
    float carX = prev.r;

    // Читаем клавиатуру
    float left  = texelFetch(iChannel1, ivec2(37,0), 0).x; // ←
    float right = texelFetch(iChannel1, ivec2(39,0), 0).x; // →

    float speed = 0.8 * iTimeDelta;

    if (left > 0.5) carX -= speed;
    if (right > 0.5) carX += speed;

    // Ограничение, чтобы машина оставалась в пределах дороги
    carX = clamp(carX, -0.45, 0.45);

    fragColor = vec4(carX, 0.0, 0.0, 1.0);
}
