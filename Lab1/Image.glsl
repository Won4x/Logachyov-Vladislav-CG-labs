// ========================
// Константы окружения
// ========================
#define ROAD_COLOR      vec3(0.3, 0.3, 0.3)
#define GRASS_COLOR     vec3(0.3, 0.65, 0.2)
#define BORDER_COLOR    vec3(0.4, 0.4, 0.5)
#define LINE_COLOR      vec3(1.3, 1.0, 0.0)

#define ROAD_WIDTH      1.0
#define BORDER_WIDTH    0.02
#define LANE_COUNT      2.0
#define LINE_SCROLL_SPEED 10.0

// ========================
// Константы машины
// ========================
#define CAR_COLOR        vec3(0.9, 0.0, 0.1)
#define CAR_WHEEL_COLOR  vec3(0.1, 0.1, 0.1)
#define CAR_GLASS_COLOR  vec3(0.5, 0.8, 1.0) // цвет стекла

#define CAR_BODY_SIZE    vec2(0.08, 0.14)

#define CAR_WHEEL_X      0.08
#define CAR_WHEEL_Y      0.09
#define CAR_WHEEL_SIZE   vec2(0.02, 0.04)

#define CAR_FRONT_GLASS_OFFSET vec2(0.0, 0.05)
#define CAR_BACK_GLASS_OFFSET  vec2(0.0, -0.08)
#define CAR_GLASS_SIZE         vec2(0.07, 0.015)

// ========================
// Константы препятствий
// ========================
#define OBSTACLE_SPEED  1.0
#define OBSTACLE_COUNT  5

#define BOX_COLOR        vec3(0.6, 0.0, 0.1)
#define BOX_INNER_COLOR  vec3(1.0)
#define CONE_COLOR       vec3(1.0, 0.5, 0.1)
#define CONE_INNER_COLOR vec3(1.0)
#define BOX_SIZE         vec2(0.1, 0.02)
#define BOX_INNER_SIZE   vec2(0.07, 0.02)
#define CONE_OUTER_RADIUS 0.04
#define CONE_INNER_RADIUS 0.025

// ========================
// Константы деревьев
// ========================
#define TREE_COUNT       10
#define TREE_RADIUS      0.15
#define TREE_COLOR       vec3(0.0, 0.5, 0.0)
#define TREE_OFFSET_X    0.2
#define TREE_OFFSET_Y    1.1

// ========================
// SDF функции
// ========================
float sdBox(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}

float sdCircle(vec2 p, float r) {
    return length(p) - r;
}

// ========================
// Машина
// ========================
float sdCar(vec2 uv, out int part) {
    part = -1;

    if (sdBox(uv - CAR_FRONT_GLASS_OFFSET, CAR_GLASS_SIZE) < 0.0) { part = 2; return -1.0; }
    if (sdBox(uv - CAR_BACK_GLASS_OFFSET, CAR_GLASS_SIZE) < 0.0) { part = 2; return -1.0; }
    if (sdBox(uv, CAR_BODY_SIZE) < 0.0) { part = 0; return -1.0; }

    if (sdBox(uv - vec2( CAR_WHEEL_X,  CAR_WHEEL_Y), CAR_WHEEL_SIZE) < 0.0 ||
        sdBox(uv - vec2( CAR_WHEEL_X, -CAR_WHEEL_Y), CAR_WHEEL_SIZE) < 0.0 ||
        sdBox(uv - vec2(-CAR_WHEEL_X,  CAR_WHEEL_Y), CAR_WHEEL_SIZE) < 0.0 ||
        sdBox(uv - vec2(-CAR_WHEEL_X, -CAR_WHEEL_Y), CAR_WHEEL_SIZE) < 0.0) { part = 1; return -1.0; }

    return 1.0;
}

float hash(float n) { return fract(sin(n) * 43758.5453); }

// ========================
// Дорога
// ========================
vec3 drawRoad(vec2 uv) {
    vec3 color;

    if (abs(uv.x) > ROAD_WIDTH * 0.5 + BORDER_WIDTH) {
        float scroll = iTime * LINE_SCROLL_SPEED * 0.1;
        vec2 uvTex = vec2(uv.x, uv.y + scroll) * 3.0;
        vec3 texColor = texture(iChannel3, uvTex).rgb;
        color = mix(GRASS_COLOR, texColor, 0.1);
    } else if (abs(uv.x) > ROAD_WIDTH * 0.5) {
        color = BORDER_COLOR;
    } else {
        color = ROAD_COLOR;

        float laneWidth = ROAD_WIDTH / LANE_COUNT;
        for (float i = 1.0; i < LANE_COUNT; i++) {
            float linePos = -ROAD_WIDTH * 0.5 + i * laneWidth;
            float dist = abs(uv.x - linePos);
            if (dist < 0.01 && mod(uv.y * 10.0 + iTime * LINE_SCROLL_SPEED, 2.0) < 1.0) {
                color = mix(color, LINE_COLOR, 0.8);
            }
        }
    }

    return color;
}

// ========================
// Препятствия
// ========================
vec3 drawObstacles(vec2 uv) {
    vec3 col = vec3(0.0);

    for (int i = 0; i < OBSTACLE_COUNT; i++) {
        float seed = float(i);
        float rndX = hash(seed) * (ROAD_WIDTH * 0.6) - ROAD_WIDTH * 0.3;
        float rndOffset = hash(seed + 10.0) * 8.0 + hash(seed + 20.0) * 0.5;
        float period = 2.0 + hash(seed + 30.0) * 1.5;
        float y = 1.2 - mod(iTime * OBSTACLE_SPEED + rndOffset, period);
        y += hash(seed + 40.0) * 0.2;
        vec2 pos = vec2(rndX, y);

        if (mod(seed, 2.0) < 1.0) {
            vec2 rel = uv - pos;
            float dOuter = sdBox(rel, BOX_SIZE);
            float dInner = sdBox(rel, BOX_INNER_SIZE);
            if (dOuter < 0.0) col = BOX_COLOR;
            if (dInner < 0.0) col = BOX_INNER_COLOR;
        } else {
            vec2 rel = uv - pos;
            float dOuter = sdCircle(rel, CONE_OUTER_RADIUS);
            float dInner = sdCircle(rel, CONE_INNER_RADIUS);
            float dInnerInner = sdCircle(rel, CONE_INNER_RADIUS - 0.01);
            if (dOuter < 0.0) col = CONE_COLOR;
            if (dInner < 0.0) col = CONE_INNER_COLOR;
            if (dInnerInner < 0.0) col = CONE_COLOR;
        }
    }

    return col;
}

// ========================
// Деревья
// ========================
vec3 drawTrees(vec2 uv) {
    vec3 col = vec3(0.0);

    for (int i = 0; i < TREE_COUNT; i++) {
        float seed = float(i);
        float side = (mod(seed, 2.0) < 1.0) ? -1.0 : 1.0; // слева или справа дороги
        float x = side * (ROAD_WIDTH * 0.5 + TREE_OFFSET_X);
        float y = mod(iTime * -1.0 + hash(seed) * 4.0, 4.0) - 2.0;
        vec2 pos = vec2(x, y);

        vec2 rel = uv - pos;
        if (sdCircle(rel, TREE_RADIUS) < 0.0) col = TREE_COLOR;
    }

    return col;
}


// ========================
// Главный вывод
// ========================
void mainImage(out vec4 fragColor, in vec2 fragCoord) {
    vec2 uv = (fragCoord - 0.5 * iResolution.xy) / iResolution.y;

    float carX = texelFetch(iChannel0, ivec2(0,0), 0).r;

    vec3 color = drawRoad(uv);

    vec3 obsColor = drawObstacles(uv);
    if (obsColor != vec3(0.0)) color = obsColor;

    vec3 treeColor = drawTrees(uv);
    if (treeColor != vec3(0.0)) color = treeColor;

    vec2 carUV = uv - vec2(carX, -0.3);
    int part;
    float carShape = sdCar(carUV, part);
    if (carShape < 0.0) {
        if (part == 0) color = CAR_COLOR;
        else if (part == 1) color = CAR_WHEEL_COLOR;
        else if (part == 2) color = CAR_GLASS_COLOR;
    }

    fragColor = vec4(color, 1.0);
}