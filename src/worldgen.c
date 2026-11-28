/* Build with cc -O3 worldgen.c -lm -o worldgen. Run from the game directory. */
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum { AIR, GRASS, DIRT, STONE, COPPER, IRON, COAL, LODESTONE, TIMBER, LEAF, CLAY, TILE, SAND, CRYSTAL, WATER, BEDROCK };
enum { SIDE = 32, HEIGHT = 64, TILES = 128 };
static unsigned char voxels[SIDE * HEIGHT * SIDE];

static uint32_t hash(int x, int y, int z) {
    uint32_t value = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ (uint32_t)z * 0xcb1ab31fu;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

static double field(int x, int z, double scale) {
    double gx = x / scale, gz = z / scale;
    int ix = (int)floor(gx), iz = (int)floor(gz);
    double tx = gx - ix, tz = gz - iz;
    tx = tx * tx * (3 - 2 * tx);
    tz = tz * tz * (3 - 2 * tz);
    double a = (hash(ix, 17, iz) & 65535) / 65535.0;
    double b = (hash(ix+1, 17, iz) & 65535) / 65535.0;
    double c = (hash(ix, 17, iz+1) & 65535) / 65535.0;
    double d = (hash(ix+1, 17, iz+1) & 65535) / 65535.0;
    return (1-tz)*(a+(b-a)*tx) + tz*(c+(d-c)*tx);
}

static int height_at(int x, int z) {
    double radius = hypot(x, z);
    double normal = 12 + 24*field(x,z,195) + 11*field(x,z,43) + 4*field(x,z,11);
    double blend = fmin(1, fmax(0, (radius-26)/47));
    return (int)round(28 + (normal-28)*blend);
}

static int geology(int x, int y, int z) {
    uint32_t vein = hash(x/5,y/3,z/5) % 100;
    uint32_t speck = hash(x,y,z) % 100;
    if (vein < 8 && speck < 65) return COPPER;
    if (vein < 17 && vein >= 8 && speck < 70) return IRON;
    if (vein >= 17 && vein < 24 && speck < 80) return COAL;
    if (vein >= 24 && vein < 28 && speck < 65) return LODESTONE;
    if (y < 22 && vein > 94 && speck < 70) return CRYSTAL;
    return STONE;
}

static int teach(int x, int y, int z, int value) {
    if (x < -7 || x > 7 || z < -9 || z > 8) return value;
    if (y == 28) return TILE;
    if (y < 29 || y > 34) return value;
    if ((x == -7 || x == 7 || z == -9 || (z == 8 && abs(x) > 2)) && y <= 33) {
        return y == 33 || ((x+z)&3) == 0 ? STONE : TILE;
    }
    if (y == 29 || y == 30) {
        int material = AIR;
        if (abs(z+4) <= 1 && x >= -5 && x <= -3) material = TIMBER;
        if (abs(z-1) <= 1 && x >= -5 && x <= -3) material = STONE;
        if (abs(z+4) <= 1 && x >= 3 && x <= 5) material = COPPER;
        if (abs(z-1) <= 1 && x >= 3 && x <= 5) material = IRON;
        if (abs(z-5) <= 1 && x >= -5 && x <= -3) material = CLAY;
        if (abs(z-5) <= 1 && x >= 3 && x <= 5) material = LODESTONE;
        return material;
    }
    return AIR;
}

static int column(int x, int z, int height, int y, int forest) {
    if (y == 0) return BEDROCK;
    if (y <= height) {
        if (y < height-4 && y > 3) {
            int cavern = ((hash(x/6, y/5, z/6) >> 15) & 1023) < 86;
            if (cavern) return AIR;
        }
        if (y == height) return height < 23 ? SAND : GRASS;
        if (y > height - 4) return (height < 25 || hash(x/6,3,z/6)%100 < 12) ? CLAY : DIRT;
        return geology(x,y,z);
    }
    if (y <= 22) return WATER;
    if (forest && y <= height+5) {
        int gx = (x + 4096) / 7, gz = (z + 4096) / 7;
        int tx = gx*7-4093, tz = gz*7-4093;
        if (hash(gx,2,gz)%100 < 26) {
            int dx = abs(x-tx), dz = abs(z-tz);
            if (dx == 0 && dz == 0 && y <= height+4) return TIMBER;
            if (y >= height+3 && dx+dz<=3 && dx<3 && dz<3) return LEAF;
        }
    }
    return AIR;
}

/* A walked route to an open quarry and a crawlspace under its eastern wall. */
static int quarry(int x, int y, int z, int value) {
    if (z < 14 || z > 34 || x < 2 || x > 83) return value;

    if (abs(z-24) <= 1 && x <= 34) {
        const int grade = x < 27 ? 28 : 27;
        if (y == grade) return TILE;
        if (y > grade && y <= grade+5) return AIR;
        if (y == grade-1 && value == AIR) return STONE;
    }
    /* Marker pillars flank the first bend, clear of the trail. */
    if ((x==7 || x==32) && abs(z-24)==3 && y>=27 && y<=30) return y==30 ? COPPER : TILE;

    if (x>=34 && x<=58 && abs(z-24)<=2) {
        const int grade=28-(x-34)/2;
        if (y == grade) return (x%2==0 || y>=26) ? TILE : STONE;
        if (y == grade-1 && value==AIR) return STONE;
        if (y > grade) return AIR;
    }

    const int dx=x-57, dz=z-24;
    if (dx*dx*49+dz*dz*64<=64*49) {
        if (y==16) return STONE;
        if (y>16) return AIR;
    }
    if (x>=53 && x<=61 && (z==17 || z==31) && y>=17 && y<=21) {
        if (z==17) return (x%3==0 || y==17) ? STONE : COPPER;
        return (x%3==0 || y==17) ? STONE : (x%2==0 ? IRON : COAL);
    }

    if (x>=63 && x<=73 && z>=23 && z<=24) {
        if (y==16) return STONE;
        if (y==17) return AIR;
        if (y==18 && x>=65 && x<=71) return STONE;
        if (y>=19 && y<=21 && x<=64) return AIR;
    }
    if (x>=72 && x<=82 && z>=20 && z<=28) {
        if (y==16) return STONE;
        if (y>=17 && y<=20) return AIR;
        if (y==21 && (x+z)%4==0) return CRYSTAL;
        if (y==22 && value==AIR) return STONE;
    }
    if ((x==82 || (x>=74 && (z==19 || z==29))) && y>=17 && y<=20) {
        if ((x+z+y)%3==0) return CRYSTAL;
    }
    return value;
}

int main(void) {
    for (int tz = -TILES/2; tz < TILES/2; tz++) {
        char path[80];
        snprintf(path,sizeof path,"world/row-%d.bin",tz);
        FILE *file = fopen(path,"wb");
        if (!file) { perror(path); return 1; }
        for (int tx=-TILES/2; tx<TILES/2; tx++) {
            for (int lz=0; lz<SIDE; lz++) for (int lx=0; lx<SIDE; lx++) {
                int x=tx*SIDE+lx, z=tz*SIDE+lz, h=height_at(x,z);
                int forest = (abs(x)>14 || abs(z)>15) && h>23 && h<47 && field(x,z,80) > 0.31;
                for (int y=0; y<HEIGHT; y++) {
                    int value = column(x,z,h,y,forest);
                    if (y==h && z>=12 && z<=26 && abs(x)<=2) value = TILE;
                    if (y==29 && x>=-6 && x<=6 && z>=13 && z<=16) {
                        if (x>=-6 && x<=-4) value = COAL;
                        if (x>=4 && x<=6) value = TIMBER;
                    }
                    if (y==29 && x>=-6 && x<=6 && z>=19 && z<=23) {
                        if (x>=-6 && x<=-4) value = COPPER;
                        if (x>=4 && x<=6) value = IRON;
                    }
                    voxels[(lz*SIDE+lx)*HEIGHT+y] = quarry(x,y,z,teach(x,y,z,value));
                }
            }
            if (fwrite(voxels,1,sizeof voxels,file) != sizeof voxels) { perror(path); return 1; }
        }
        if (fclose(file)) { perror(path); return 1; }
    }
    return 0;
}
