-- pacman_scenes.lua - MAME autoboot script: records the scenes of the Pac-Man
-- arcade that are not the game for pacman.c (pacman_scene.py converts
-- the log): the attract mode up to the demo game (the characters and their
-- nicknames, the chase of the power pellet) and the three intermissions with
-- their music.
--
--   mame pacman -rompath DIR -video none -sound none -nothrottle \
--        -skip_gameinfo -seconds_to_run 200 -autoboot_script pacman_scenes.lua
--   (PACSCN_OUT = the log file, default pacman_scenes.log)
--
-- The attract mode runs from the end of the boot test ($4E00 = 1, $4E02 =
-- 1, after $4E00/$4E02/$4E04 were all 0) until the demo game starts
-- ($4E02 = $23).  Then a game is started (coin, start) and the levels 2, 5
-- and 9 are ended at once, writing the level ($4E13, from 0) and the dots
-- eaten ($4E0E = $F4) while Pac-Man plays ($4E04 = 3): the game plays
-- intermission 1, 2 and 3 ($4E04 = $20 while they run).  Logged every frame:
--
--   V <frame> <addr>=<value> ...   the video RAM bytes changed since the last
--                                  frame: 000-3FF tiles, 400-7FF colours
--   S <frame> <sprite codes $4FF0-$4FFF> <positions $5060-$506F>
--     <$4E00 $4E02 $4E04> <16 nibbles $4E8C-$4E9B> <3 waveforms>
--     <requests $4E9C $4EAC $4EBC $4ECC $4EDC $4EEC>   (as pacman_rip.lua)
--   M <frame> <name>               attract, attract_end, act1/2/3, act_end

local m = manager.machine
local sp = m.devices[":maincpu"].spaces["program"]
local sh = m.memory.shares
local spr, pos = sh[":spriteram"], sh[":spriteram2"]
local vram, cram = sh[":videoram"], sh[":colorram"]
local out = io.open(os.getenv("PACSCN_OUT") or "pacman_scenes.log", "w")

local frame = 0
local prev = {}
local acts = { 1, 4, 8 }        -- the levels ended (from 0): after them acts 1, 2, 3
local act = 0                   -- acts started
local in_act = false
local attract = 0               -- 0 before, 1 recording, 2 done
local was_boot = false          -- $4E00, $4E02 and $4E04 were 0: the boot test is over
local coin_at = nil
local stop_at = nil

local function rd(a) return sp:read_u8(a) end

local function press(port, field, v)
    m.ioport.ports[port].fields[field]:set_value(v)
end

local function hex(share, n)
    local s = {}
    for a = 0, n - 1 do s[#s + 1] = string.format("%02x", share:read_u8(a)) end
    return table.concat(s)
end

local function wave(song, songw, fxw)
    if rd(song) ~= 0 then return rd(songw) & 7 end
    return rd(fxw) & 7
end

local function log_frame()
    local d = {}
    for a = 0, 0x7ff do
        local v = a < 0x400 and vram:read_u8(a) or cram:read_u8(a - 0x400)
        if prev[a] ~= v then
            prev[a] = v
            d[#d + 1] = string.format("%03x=%02x", a, v)
        end
    end
    if #d > 0 then out:write(string.format("V %d %s\n", frame, table.concat(d, " "))) end
    local n = {}
    for a = 0x4e8c, 0x4e9b do n[#n + 1] = string.format("%x", rd(a) & 15) end
    out:write(string.format("S %d %s %s %02x %02x %02x %s %d%d%d %02x %02x %02x %02x %02x %02x\n",
        frame, hex(spr, 16), hex(pos, 16), rd(0x4e00), rd(0x4e02), rd(0x4e04), table.concat(n),
        wave(0x4ecc, 0x4ecf, 0x4e9f), wave(0x4edc, 0x4edf, 0x4eaf), wave(0x4eec, 0x4eef, 0x4ebf),
        rd(0x4e9c), rd(0x4eac), rd(0x4ebc), rd(0x4ecc), rd(0x4edc), rd(0x4eec)))
end

local function on_frame()
    frame = frame + 1
    log_frame()

    if frame > 100 and rd(0x4e00) == 0 and rd(0x4e02) == 0 and rd(0x4e04) == 0 then was_boot = true end
    if attract == 0 and was_boot and rd(0x4e00) == 1 and rd(0x4e02) == 1 then
        attract = 1
        out:write(string.format("M %d attract\n", frame))
    elseif attract == 1 and rd(0x4e02) == 0x23 then
        attract = 2
        out:write(string.format("M %d attract_end\n", frame))
        coin_at = frame + 30
    end
    if coin_at then
        local t = frame - coin_at
        if t == 0 then press(":IN0", "Coin 1", 1) end
        if t == 10 then press(":IN0", "Coin 1", 0) end
        if t == 60 then press(":IN1", "1 Player Start", 1) end
        if t == 70 then press(":IN1", "1 Player Start", 0) end
    end

    -- end the level as soon as Pac-Man plays
    if attract == 2 and rd(0x4e00) == 3 and rd(0x4e04) == 3 and act < #acts and not in_act
            and rd(0x4e13) ~= acts[act + 1] + 1 then
        sp:write_u8(0x4e13, acts[act + 1])
        sp:write_u8(0x4e0e, 0xf4)
    end
    if not in_act and rd(0x4e00) == 3 and rd(0x4e04) == 0x20 then
        in_act = true
        act = act + 1
        out:write(string.format("M %d act%d\n", frame, act))
    elseif in_act and rd(0x4e04) ~= 0x20 then
        in_act = false
        out:write(string.format("M %d act_end\n", frame))
        if act == #acts then stop_at = frame + 60 end   -- the end of the song
    end
    if frame == stop_at or frame > 12000 then
        out:close()
        m:exit()
    end
end

-- register_frame_done was replaced by add_machine_frame_notifier in newer MAME
if emu.add_machine_frame_notifier then
    scn_sub = emu.add_machine_frame_notifier(on_frame)
else
    emu.register_frame_done(on_frame)
end
