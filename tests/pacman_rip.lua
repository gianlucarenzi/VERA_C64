-- pacman_rip.lua - MAME autoboot script: rips the sounds of the Pac-Man arcade
-- for pacman.c (pacman_snd.py converts the log to SID data).
--
--   mame pacman -rompath DIR -video none -sound none -nothrottle \
--        -seconds_to_run 120 -autoboot_script pacman_rip.lua
--   (PACRIP_OUT = the log file, default pacman_rip.log)
--
-- Pac-Man has no sound CPU: the IRQ copies the 16 frequency/volume nibbles of
-- the Namco WSG from RAM $4E8C-$4E9B to $5050-$505F, and picks the waveforms
-- for $5045/$504A/$504F from $4ECF/$4EDF/$4EEF while a song plays on the
-- voice ($4ECC/$4EDC/$4EEC != 0), from $4E9F/$4EAF/$4EBF otherwise.  The WSG
-- registers are write only, so their RAM copy is logged once per frame:
--
--   F <frame> <16 nibbles $4E8C-$4E9B> <3 waveforms> <requests $4E9C $4EAC $4EBC $4ECC $4EDC $4EEC>
--   M <frame> <name>        a marker: the script requested sound <name>
--
-- The requests: $4E9C voice 0 effects, $4EAC voice 1 (continuous: siren,
-- frightened ghosts, eyes), $4EBC voice 2 (one shot: dot, fruit, ghost,
-- death), $4ECC-$4EEC the songs.  Out of a game the driver ignores voices 1
-- and 2, so a game is started (coin, start) and the effects are requested
-- while Pac-Man, with no joystick input, stands against a wall; the siren
-- levels 2-5 come from the game itself, with $4E0E (dots eaten) past its
-- thresholds $74/$B4/$D4/$E4.  The intro, siren 1 and the death are played
-- by the game.

local m = manager.machine
local sp = m.devices[":maincpu"].spaces["program"]
local out = io.open(os.getenv("PACRIP_OUT") or "pacman_rip.log", "w")

local frame = 0
local intro = nil           -- frame of the intro start: the schedule is relative to it

-- {start, frames, address, value, name}: value written every frame
local schedule = {
    { 360,   3, 0x4ebc, 0x01, "dot1" },
    { 400,   3, 0x4ebc, 0x02, "dot2" },
    { 440,   3, 0x4ebc, 0x04, "fruit" },
    { 500,   3, 0x4ebc, 0x08, "ghost" },
    { 560,   1, 0x4e9c, 0x01, "xlife" },
    { 1380, 140, 0x4e0e, 0x80, "siren2" },
    { 1530, 140, 0x4e0e, 0xb8, "siren3" },
    { 1680, 140, 0x4e0e, 0xd8, "siren4" },
    { 1830, 140, 0x4e0e, 0xe8, "siren5" },
    { 2480, 220, 0x4eac, 0x20, "fright" },
    { 2750, 300, 0x4eac, 0x40, "eyes" },
}
local last = 3100

local function press(port, field, v)
    m.ioport.ports[port].fields[field]:set_value(v)
end

local function rd(a) return sp:read_u8(a) end

local function wave(song, songw, fxw)
    if rd(song) ~= 0 then return rd(songw) & 7 end
    return rd(fxw) & 7
end

local function on_frame()
    frame = frame + 1
    if frame == 360 then press(":IN0", "Coin 1", 1) end
    if frame == 370 then press(":IN0", "Coin 1", 0) end
    if frame == 420 then press(":IN1", "1 Player Start", 1) end
    if frame == 430 then press(":IN1", "1 Player Start", 0) end
    if intro == nil and frame > 430 and rd(0x4ecc) ~= 0 then
        intro = frame
        out:write(string.format("M %d intro\n", frame))
    end
    if intro ~= nil then
        local t = frame - intro
        for _, s in ipairs(schedule) do
            if t == s[1] then out:write(string.format("M %d %s\n", frame, s[5])) end
            if t >= s[1] and t < s[1] + s[2] then sp:write_u8(s[3], s[4]) end
        end
    end
    local n = {}
    for a = 0x4e8c, 0x4e9b do n[#n + 1] = string.format("%x", rd(a) & 15) end
    out:write(string.format("F %d %s %d%d%d %02x %02x %02x %02x %02x %02x\n", frame, table.concat(n),
        wave(0x4ecc, 0x4ecf, 0x4e9f), wave(0x4edc, 0x4edf, 0x4eaf), wave(0x4eec, 0x4eef, 0x4ebf),
        rd(0x4e9c), rd(0x4eac), rd(0x4ebc), rd(0x4ecc), rd(0x4edc), rd(0x4eec)))
    if (intro ~= nil and frame - intro >= last) or frame > 6000 then
        out:close()
        m:exit()
    end
end

-- register_frame_done was replaced by add_machine_frame_notifier in newer MAME
if emu.add_machine_frame_notifier then
    rip_sub = emu.add_machine_frame_notifier(on_frame)
else
    emu.register_frame_done(on_frame)
end
