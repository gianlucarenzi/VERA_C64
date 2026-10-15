# VIC-II Screen Mirroring on VERA

This guide explains why the VERA screen driver doesn't mirror the VIC-II screen, the technical limitations, and practical approaches to achieve it.

## The Problem: Geometry Mismatch

### VIC-II (C64 Native)
- **Resolution**: 320×200 pixels
- **Text mode**: 40 columns × 25 rows
- **Character size**: 8×8 pixels
- **Standard aspect ratio**: 4:3 (720×576 with borders)

### VERA Display Modes
- **vera80**: 640×480, 80×30 characters (8×16 font)
- **vera60**: 640×480, 80×60 characters (8×8 font)
- **vera40**: 320×240, 40×30 characters (8×8 font, 2× scaled)

### Why No Direct Mirroring?

The VIC-II geometry (40×25) doesn't align naturally with VERA's resolutions:
- **40×25** (VIC) → **80×60** (VERA full): Would need 2× horizontal and 2.4× vertical scaling
- **40×25** (VIC) → **80×30** (VERA driver): Would need 2× horizontal but only 1.2× vertical
- **40×25** (VIC) → **40×30** (VERA scaled): Only 25 rows fit, wasting space

The VERA screen driver deliberately avoids mirroring to prevent:
1. **Inconsistency**: Different rendering between VIC and VERA
2. **Memory waste**: Duplicating 1KB of screen and 256 bytes of color
3. **Synchronization issues**: Keeping two displays in sync under KERNAL hooks

## Practical Solutions

### Solution 1: Simple 1:1 Mapping with Padding

**Approach**: Display VIC screen 1:1 on VERA, leaving margins.

**Pros:**
- Simple implementation
- No scaling distortion
- Easy to understand

**Cons:**
- Wasted screen space
- Doesn't use VERA's capabilities

**Implementation** (40×25 → 40×30):
```
VERA Layer 1:
┌─────────────────────────┐
│  (empty row)            │
│  [VIC row 0-24]         │
│  (empty rows)           │
└─────────────────────────┘
   40 columns × 30 rows
```

**Code example**: See `tests/vic_mirror.c`

```c
void mirror_line(unsigned char vic_row, unsigned char vera_row) {
    for (int col = 0; col < 40; col++) {
        unsigned char ch = VIC_SCREEN[vic_row * 40 + col];
        vera_put_char(vera_row, col, ch);
    }
}
```

### Solution 2: Scaled 2×2 Display

**Approach**: Double each character horizontally and vertically.

**Pros:**
- Uses more VERA screen space
- Makes small text more readable
- Better utilization

**Cons:**
- More complex (requires interpolation/duplication)
- Takes up 80×50 of VERA's 80×60
- Still leaves vertical gap

**Implementation** (40×25 → 80×50):
```c
void mirror_line_scaled(unsigned char vic_row, unsigned char vera_row) {
    for (int col = 0; col < 40; col++) {
        unsigned char ch = VIC_SCREEN[vic_row * 40 + col];
        unsigned char color = VIC_COLOR[vic_row * 40 + col];
        
        /* Write 2x horizontally */
        vera_put_char(vera_row, col * 2, ch);
        vera_put_char(vera_row, col * 2 + 1, ch);
        
        /* Write 2x vertically (next line) */
        if (vera_row + 1 < 60) {
            vera_put_char(vera_row + 1, col * 2, ch);
            vera_put_char(vera_row + 1, col * 2 + 1, ch);
        }
    }
}
```

### Solution 3: Hook KERNAL Vectors (Best)

**Approach**: Like `vera_screen.s`, hook CHROUT/CHRIN vectors to capture screen changes in real-time.

**Pros:**
- Real-time mirroring
- Efficient (only updates changed cells)
- Can use full VERA resolution
- Transparent to programs

**Cons:**
- Complex assembly code
- Must handle all C64 screen codes
- IRQ-safe operations required

**Vectors to hook:**
| Vector | Address | Purpose |
|--------|---------|---------|
| IBSOUT | $0326 | Character output (PRINT) |
| IBASIN | $0324 | Character input (INPUT) |
| CINV | $0314 | IRQ handler (cursor blink) |
| IERROR | $0300 | Error handler (RUN/STOP+RESTORE) |

**Pseudocode:**
```
CHROUT_HOOK:
    if (device == 3) {  // Screen output
        output_to_vera(character);
        output_to_vic(character);  // Still use VIC for reference
    } else {
        jmp old_chrout;
    }
```

This is exactly what `driver/vera_screen.s` does!

## Geometry Comparison

| Aspect | VIC-II | VERA (40) | VERA (80) |
|--------|--------|-----------|-----------|
| Columns | 40 | 40 | 80 |
| Rows | 25 | 30 | 30/60 |
| Aspect | 1:1 | 1:1 | 2:1 (needs scaling) |
| Color depth | 4 bits (16 colors) | 8 bits (256 colors) | 8 bits (256 colors) |
| Font size | 8×8 | 8×8 | 8×8 or 8×16 |

## Character Encoding

Both VIC and VERA use **C64 screen codes** (PETSCII subset), but differ in:

**Uppercase/Graphics** (default on both):
- `$00-$3F`: Graphics characters (⌜⌟⌞⌝ ▔▁▖▗ etc.)
- `$40-$7F`: Uppercase letters and symbols

**Lowercase** (activated with `POKE 646,14` or case key):
- `$00-$3F`: Graphics (same)
- `$40-$5F`: Uppercase letters
- `$60-$7F`: Lowercase letters

**Reverse** (activated with `POKE 646,128` or RVS ON):
- Invert all characters (top half of character ROM)

When mirroring, ensure the VERA font has both cases and reversed variants.

## Color Mapping

### VIC-II Color Limitations
- **16 colors** total (C64 palette)
- **Global background** ($D021): One color for entire screen
- **Per-character foreground** ($D800-$D8E7): 1 byte per character

### VERA Color Capabilities
- **256 colors** (full palette access)
- **Per-cell foreground AND background**
- **More flexibility** for rendering

**Mapping strategy:**
```
VIC Color RAM ($D800) → VERA Color Cell (2 bytes per tile)

VIC: char + color       VERA: char (1 byte) + color (1 byte)
      C64 palette    →        Mapped to entry in VERA palette
      (16 colors)            (256 colors available)
```

## Performance Considerations

### Memory Usage
```
VIC Screen:   1 KB (40×25 chars)
VIC Color:    256 B (40×25 colors)
Total:        1.25 KB

VERA Mirror (40×30):
  Tilemap:    2.4 KB (40×30 × 2 bytes)
  Font:       4 KB (256 chars × 16 bytes, if 8×16)
  Total:      6.4 KB
```

### Update Performance

**Polling approach** (simple):
- Scan entire VIC screen every frame: ~40×25 = 1000 checks
- Slow, CPU-intensive
- Updates delayed by scan rate

**Vector hook approach** (efficient):
- Only update changed cells (via CHROUT hook)
- Real-time synchronization
- Better for interactive use

**Example timings** (on 1 MHz 6502):
- Read VIC cell: 3 cycles
- Write VERA cell: 4 cycles
- Mirror entire screen: ~7000 cycles = ~7ms (70Hz frame rate impact)

## Example: Simple Mirror Program

See `tests/vic_mirror.c` for a complete implementation:

```bash
# Compile
cd tests
make vic_mirror.prg

# Run on VICE
x64sc -vera -autostart vic_mirror.prg &
```

**Features:**
- Sets up VERA layer 1 for text mode
- Mirrors VIC screen 1:1 on startup
- Demonstrates character and color access
- Shows VERA register manipulation

## Advanced: Dual Display Mirroring

The RMT player (`rmt/rmtplay.prg`) demonstrates a practical dual-display approach:

```
┌─────────────────────────┐
│   VERA Display          │     Real-time visualizer
│   (rmtplay overlay)     │     Layer 0 or layer 1
│                         │
│   VIC-II Screen         │     Reference only
│   (still working)       │     User BASIC session
└─────────────────────────┘
```

Both displays show different content:
- **VERA**: Music visualizer (frequency bars, song info)
- **VIC**: User continues normal C64 operation

This is more practical than true mirroring.

## When to Use Mirroring

### Good Use Cases
✅ Debuggers (showing program state on both screens)
✅ Demo effects (artistic dual-screen visuals)
✅ Terminal emulation (large monitor preview)
✅ Game development (testing layouts)

### Poor Use Cases
❌ Everyday screen driver (wastes resources)
❌ BASIC sessions (conflicts with KERNAL)
❌ Programs writing directly to VIC (sync issues)

## Related Resources

- **Driver source**: `driver/vera_screen.s` - Vector hooking pattern
- **Vector documentation**: $0300-$0326 KERNAL jump vectors
- **Font handling**: `driver/font/petscii.s` - Character encoding
- **Dual display example**: `rmt/rmtplay.c` - Practical implementation

## Summary

| Approach | Complexity | Efficiency | Use Case |
|----------|-----------|-----------|----------|
| 1:1 Padding | ⭐ Low | Low | Testing, simple displays |
| 2×2 Scaling | ⭐⭐ Medium | Medium | Demo effects |
| Vector Hook | ⭐⭐⭐ High | High | Real-time applications |
| Dual Content | ⭐⭐ Medium | High | Mixed applications (RMT player) |

The best approach depends on your needs. For most applications, keeping VIC and VERA separate (as in `vera_screen.s`) is cleaner than trying to mirror them.

---

**Documentation for VERA_C64 VIC Mirroring - 2024**
