#!/usr/bin/env python3

import argparse
import json
import math
import wave
from pathlib import Path


TABLE_SIZE = 256
GRID_SIZE = 4
TABLE_COUNT = GRID_SIZE * GRID_SIZE
SAMPLE_RATE = 48000
MAX_HARMONIC = 96
TAU = math.tau

SINE_BASIS = [
    [math.sin(TAU * harmonic * sample / TABLE_SIZE) for sample in range(TABLE_SIZE)]
    for harmonic in range(MAX_HARMONIC + 1)
]
COSINE_BASIS = [
    [math.cos(TAU * harmonic * sample / TABLE_SIZE) for sample in range(TABLE_SIZE)]
    for harmonic in range(MAX_HARMONIC + 1)
]


def clamp(value, minimum, maximum):
    return max(minimum, min(maximum, value))


def lerp(a, b, amount):
    return a + (b - a) * amount


def smoothstep(value):
    value = clamp(value, 0.0, 1.0)
    return value * value * (3.0 - 2.0 * value)


def hash01(value):
    value = (value ^ 0x9E3779B9) & 0xFFFFFFFF
    value = (value ^ (value >> 16)) * 0x21F0AAAD & 0xFFFFFFFF
    value = (value ^ (value >> 15)) * 0x735A2D97 & 0xFFFFFFFF
    value ^= value >> 15
    return (value & 0xFFFFFF) / float(0xFFFFFF)


def table_from_coefficients(sine_coefficients, cosine_coefficients=None):
    cosine_coefficients = cosine_coefficients or {}
    result = [0.0] * TABLE_SIZE
    for harmonic, amplitude in sine_coefficients.items():
        if 1 <= harmonic <= MAX_HARMONIC and amplitude != 0.0:
            basis = SINE_BASIS[harmonic]
            for sample in range(TABLE_SIZE):
                result[sample] += amplitude * basis[sample]
    for harmonic, amplitude in cosine_coefficients.items():
        if 1 <= harmonic <= MAX_HARMONIC and amplitude != 0.0:
            basis = COSINE_BASIS[harmonic]
            for sample in range(TABLE_SIZE):
                result[sample] += amplitude * basis[sample]
    return result


def center_table(table):
    mean = sum(table) / len(table)
    return [sample - mean for sample in table]


def level_match_atlas(tables, preferred_rms=0.30, peak_limit=0.90):
    centered = [center_table(table) for table in tables]
    measurements = []
    for table in centered:
        rms = math.sqrt(sum(sample * sample for sample in table) / len(table))
        peak = max(abs(sample) for sample in table)
        if rms < 1.0e-9 or peak < 1.0e-9:
            raise RuntimeError("VOT atlas contains an empty table")
        measurements.append((rms, peak))
    maximum_crest = max(peak / rms for rms, peak in measurements)
    target_rms = min(preferred_rms, peak_limit / maximum_crest)
    return [
        [sample * target_rms / measurements[index][0] for sample in table]
        for index, table in enumerate(centered)
    ]


def foundation(u, v):
    coefficients = {1: 1.0}
    brightness = smoothstep(u)
    slope = lerp(3.4, 1.15, brightness)
    maximum = max(2, round(lerp(4.0, 72.0, brightness)))
    for harmonic in range(2, maximum + 1):
        odd_weight = 1.0 if harmonic % 2 else lerp(1.0, 0.04, v)
        coefficients[harmonic] = brightness * odd_weight / (harmonic ** slope)
    return table_from_coefficients(coefficients)


def body_bass(u, v):
    coefficients = {1: 1.0}
    brightness = smoothstep(u)
    resonance = lerp(2.0, 7.0, v)
    for harmonic in range(2, 49):
        body = math.exp(-harmonic * lerp(0.34, 0.075, brightness)) / (harmonic ** 0.55)
        bump = 1.0 + 1.8 * math.exp(-0.5 * ((harmonic - resonance) / 1.15) ** 2)
        even = lerp(0.35, 1.15, v) if harmonic % 2 == 0 else 1.0
        coefficients[harmonic] = brightness * body * bump * even
    return table_from_coefficients(coefficients)


def pulse_reed(u, v):
    duty = lerp(0.48, 0.075, v)
    cutoff = lerp(5.0, 82.0, smoothstep(u))
    cosine = {}
    for harmonic in range(1, MAX_HARMONIC + 1):
        edge = math.exp(-((harmonic / cutoff) ** 2.0))
        cosine[harmonic] = 2.0 * math.sin(math.pi * harmonic * duty) * edge / (math.pi * harmonic)
    return table_from_coefficients({}, cosine)


def formant(u, v):
    coefficients = {1: 0.58}
    first = lerp(2.2, 11.0, u)
    second = lerp(18.0, 6.5, v)
    bandwidth = lerp(2.8, 0.75, v)
    for harmonic in range(2, 65):
        floor = 0.08 / (harmonic ** 1.15)
        f1 = 0.72 * math.exp(-0.5 * ((harmonic - first) / bandwidth) ** 2)
        f2 = 0.46 * math.exp(-0.5 * ((harmonic - second) / (bandwidth * 1.45)) ** 2)
        coefficients[harmonic] = floor + f1 + f2
    return table_from_coefficients(coefficients)


ORGAN_ROWS = (
    {1: 1.0, 2: 0.20, 3: 0.08, 4: 0.04},
    {1: 0.82, 2: 0.58, 3: 0.25, 4: 0.18, 6: 0.08},
    {1: 0.62, 2: 0.52, 3: 0.46, 4: 0.28, 6: 0.20, 8: 0.10},
    {1: 0.52, 2: 0.40, 3: 0.50, 4: 0.34, 5: 0.22, 6: 0.28, 8: 0.18, 12: 0.10, 16: 0.07},
)


def organ(u, v):
    row = min(3, round(v * 3.0))
    coefficients = {}
    registration = lerp(0.30, 1.0, smoothstep(u))
    for harmonic, amplitude in ORGAN_ROWS[row].items():
        high_open = lerp(0.18, 1.0, registration ** max(0.0, (harmonic - 1.0) / 5.0))
        coefficients[harmonic] = amplitude * high_open
    coefficients[1] = max(coefficients.get(1, 0.0), 0.45)
    return table_from_coefficients(coefficients)


def glass_metal(u, v):
    coefficients = {1: 0.45}
    density = smoothstep(u)
    lattice = lerp(2.4, 6.8, v)
    for harmonic in range(2, MAX_HARMONIC + 1):
        nearest = round(harmonic / lattice) * lattice
        sparse = math.exp(-0.5 * ((harmonic - nearest) / lerp(0.16, 1.2, density)) ** 2)
        shimmer = 0.45 + 0.55 * hash01(harmonic * 97 + round(v * 31.0))
        coefficients[harmonic] = density * sparse * shimmer / (harmonic ** lerp(0.42, 0.78, v))
    return table_from_coefficients(coefficients)


def fold_phase(u, v):
    coefficients = {1: 1.0}
    fold = smoothstep(u)
    sine = coefficients
    cosine = {}
    for harmonic in range(2, 73):
        odd = 1.0 if harmonic % 2 else v
        envelope = math.exp(-harmonic * lerp(0.22, 0.035, fold)) / (harmonic ** 0.32)
        angle = v * harmonic * 0.23
        amplitude = fold * odd * envelope
        sine[harmonic] = amplitude * math.cos(angle)
        cosine[harmonic] = amplitude * math.sin(angle)
    return table_from_coefficients(sine, cosine)


def digital(u, v):
    phase_steps = round(lerp(8.0, 64.0, u))
    amplitude_levels = round(lerp(3.0, 18.0, 1.0 - v))
    result = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        held_phase = math.floor(phase * phase_steps) / phase_steps
        source = lerp(math.sin(TAU * held_phase), 2.0 * held_phase - 1.0, v)
        quantized = round((source * 0.5 + 0.5) * (amplitude_levels - 1.0)) / (amplitude_levels - 1.0)
        result.append(quantized * 2.0 - 1.0)
    return result


def string(u, v):
    coefficients = {1: 1.0}
    brightness = smoothstep(u)
    pluck_position = lerp(0.12, 0.47, v)
    for harmonic in range(2, 81):
        excitation = abs(math.sin(math.pi * harmonic * pluck_position))
        damping = math.exp(-harmonic * lerp(0.20, 0.035, brightness))
        bow = lerp(1.0, 1.0 if harmonic % 2 else 0.28, v)
        coefficients[harmonic] = brightness * excitation * damping * bow / (harmonic ** 0.62)
    return table_from_coefficients(coefficients)


def rungler(u, v):
    row = min(3, round(v * 3.0))
    tap_masks = (0xB8, 0xA6, 0xE1, 0xD4)
    seeds = (0x5D, 0xA7, 0x39, 0xE3)
    state = seeds[row]
    words = []
    for _ in range(TABLE_SIZE):
        feedback_bit = bin(state & tap_masks[row]).count("1") & 1
        state = ((state << 1) & 0xFF) | feedback_bit
        words.append(state / 127.5 - 1.0)

    feedback = smoothstep(u)
    delay = 2 + row * 3
    result = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        current = words[sample]
        delayed = words[(sample - delay) % TABLE_SIZE]
        register = math.tanh((current + delayed * feedback * 1.7) * (1.2 + feedback * 3.2))
        clock = 1.0 if ((sample // max(1, 9 - row * 2)) & 1) else -1.0
        corrupted = lerp(register, register * clock, feedback * 0.48)
        anchor = math.sin(TAU * phase)
        result.append(lerp(anchor, corrupted, 0.38 + feedback * 0.58) + 0.22 * anchor)
    return result


def circular_distance(a, b, period):
    distance = abs(a - b) % period
    return min(distance, period - distance)


def barberpole(u, v):
    octave_count = 7
    center = (u % 1.0) * octave_count
    density = lerp(0.34, 1.15, v)
    coefficients = {1: 0.16}
    for octave in range(octave_count):
        harmonic = 1 << octave
        distance = circular_distance(float(octave), center, float(octave_count))
        weight = math.exp(-0.5 * (distance / density) ** 2)
        coefficients[harmonic] = coefficients.get(harmonic, 0.0) + weight
        sideband = harmonic * 3
        if sideband <= MAX_HARMONIC:
            coefficients[sideband] = coefficients.get(sideband, 0.0) + weight * v * 0.22
    return table_from_coefficients(coefficients)


def harmonic_mirage(u, v):
    row = min(3, round(v * 3.0))
    consonant_sets = (
        (4, 5, 6),
        (5, 6, 8),
        (6, 8, 10),
        (8, 10, 12),
    )
    cluster_sets = (
        (7, 8, 9, 11),
        (9, 10, 11, 13),
        (11, 12, 13, 15),
        (13, 14, 15, 17),
    )
    coefficients = {1: 0.18}
    consonance = 1.0 - smoothstep(u)
    for harmonic in consonant_sets[row]:
        coefficients[harmonic] = coefficients.get(harmonic, 0.0) + consonance * 0.72
        if harmonic * 2 <= MAX_HARMONIC:
            coefficients[harmonic * 2] = coefficients.get(harmonic * 2, 0.0) + consonance * 0.20
    for harmonic in cluster_sets[row]:
        coefficients[harmonic] = coefficients.get(harmonic, 0.0) + (1.0 - consonance) * 0.66
        if harmonic * 2 <= MAX_HARMONIC:
            coefficients[harmonic * 2] = coefficients.get(harmonic * 2, 0.0) + (1.0 - consonance) * 0.16
    return table_from_coefficients(coefficients)


def wave_terrain(u, v):
    angle = u * math.pi * 0.75
    deformation = lerp(0.15, 1.0, smoothstep(v))
    result = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        theta = TAU * phase
        x = math.cos(theta)
        y = math.sin(theta)
        xr = x * math.cos(angle) - y * math.sin(angle)
        yr = x * math.sin(angle) + y * math.cos(angle)
        terrain = math.sin(math.pi * (1.15 * xr + deformation * 0.72 * math.sin(math.pi * yr * 1.7)))
        terrain += 0.52 * math.sin(math.pi * (2.4 * yr - deformation * xr * yr * 2.8))
        terrain += 0.24 * math.sin(math.pi * (5.0 * xr + 3.0 * yr) * deformation)
        anchor = math.sin(theta)
        result.append(math.tanh(terrain * (0.85 + deformation * 1.35)) + 0.20 * anchor)
    return result


def codec_ghost(u, v):
    row = min(3, round(v * 3.0))
    block_size = (4, 8, 16, 32)[row]
    damage = lerp(0.24, 0.96, smoothstep(u))
    source = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        source.append(0.72 * math.sin(TAU * phase) + 0.28 * (2.0 * phase - 1.0))

    residual = []
    prediction = lerp(0.25, 0.97, damage)
    levels = max(3, round(lerp(24.0, 4.0, damage)))
    for sample in range(TABLE_SIZE):
        predicted = source[(sample - 1) % TABLE_SIZE] * prediction
        value = source[sample] - predicted
        quantized = round((clamp(value, -1.0, 1.0) * 0.5 + 0.5) * (levels - 1))
        residual.append(quantized / (levels - 1) * 2.0 - 1.0)

    result = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        block = sample // block_size
        stale_block = max(0, block - 1 - (row % 2))
        stale_sample = stale_block * block_size + (sample % block_size)
        packet_fault = hash01(block * 211 + row * 37) < damage * 0.62
        ghost = residual[stale_sample % TABLE_SIZE] if packet_fault else residual[sample]
        if row >= 2 and packet_fault:
            ghost *= -1.0 if block & 1 else 1.0
        anchor = math.sin(TAU * phase)
        result.append(lerp(source[sample], ghost * 1.8, damage) + 0.18 * anchor)
    return result


def codec_conceal(u, v):
    row = min(3, round(v * 3.0))
    block_size = (8, 12, 16, 24)[row]
    damage = lerp(0.16, 0.98, smoothstep(u))
    source = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        source.append(
            0.70 * math.sin(TAU * phase)
            + 0.20 * math.sin(TAU * phase * (3 + row))
            + 0.10 * (2.0 * phase - 1.0)
        )

    result = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        block = sample // block_size
        local_sample = sample % block_size
        local_phase = local_sample / max(1, block_size - 1)
        fault_seed = hash01(block * 257 + row * 101)
        fault = smoothstep(clamp((damage - fault_seed) * 2.4, 0.0, 1.0))
        previous_index = (sample - block_size) % TABLE_SIZE
        if row == 0:
            concealed = source[previous_index]
        elif row == 1:
            block_start = block * block_size
            before = source[(block_start - 1) % TABLE_SIZE]
            after = source[(block_start + block_size) % TABLE_SIZE]
            concealed = lerp(before, after, smoothstep(local_phase))
        elif row == 2:
            block_start = block * block_size
            before = source[(block_start - 1) % TABLE_SIZE]
            earlier = source[(block_start - 2) % TABLE_SIZE]
            slope = before - earlier
            concealed = before + slope * (local_sample + 1) * (1.0 - local_phase * 0.82)
        else:
            previous_start = (block - 1) * block_size
            reverse_index = (previous_start + block_size - 1 - local_sample) % TABLE_SIZE
            polarity = -1.0 if block & 1 else 1.0
            concealed = source[reverse_index] * polarity
        anchor = math.sin(TAU * phase)
        result.append(lerp(source[sample], concealed, fault) + 0.18 * anchor)
    return result


def codec_residual(u, v):
    row = min(3, round(v * 3.0))
    emphasis = lerp(0.20, 0.98, smoothstep(u))
    source = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        source.append(
            0.68 * math.sin(TAU * phase)
            + 0.22 * math.sin(TAU * phase * 3.0)
            + 0.10 * math.sin(TAU * phase * (7.0 + row * 2.0))
        )

    levels = max(4, round(lerp(32.0, 5.0, emphasis)))
    result = []
    for sample in range(TABLE_SIZE):
        one = source[(sample - 1) % TABLE_SIZE]
        two = source[(sample - 2) % TABLE_SIZE]
        if row == 0:
            predicted = one * 0.92
        elif row == 1:
            predicted = one * 1.55 - two * 0.62
        elif row == 2:
            predicted = two * 0.72 + source[(sample - 5) % TABLE_SIZE] * 0.20
        else:
            predicted = (
                one * 0.50
                + source[(sample - 3) % TABLE_SIZE] * 0.30
                + source[(sample - 7) % TABLE_SIZE] * 0.15
            )
        residual = clamp((source[sample] - predicted) * 2.4, -1.0, 1.0)
        quantized = round((residual * 0.5 + 0.5) * (levels - 1)) / (levels - 1) * 2.0 - 1.0
        phase = sample / TABLE_SIZE
        anchor = math.sin(TAU * phase)
        result.append(lerp(source[sample], quantized, emphasis) + 0.20 * anchor)
    return result


def codec_residual_delta(u, v):
    row = min(3, round(v * 3.0))
    emphasis = lerp(0.16, 0.98, smoothstep(u))
    source = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        source.append(
            0.70 * math.sin(TAU * phase)
            + 0.20 * math.sin(TAU * phase * (3.0 + row))
            + 0.10 * math.sin(TAU * phase * (9.0 + row * 4.0))
        )

    delta = source[:]
    for _ in range(row + 1):
        delta = [
            delta[sample] - delta[(sample - 1) % TABLE_SIZE]
            for sample in range(TABLE_SIZE)
        ]
    delta_peak = max(abs(value) for value in delta)
    delta_scale = 1.0 / max(delta_peak, 1.0e-9)

    result = []
    drive = lerp(1.15, 3.8, emphasis) + row * 0.30
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        shaped = math.tanh(delta[sample] * delta_scale * drive)
        anchor = math.sin(TAU * phase)
        result.append(lerp(source[sample], shaped, emphasis) + 0.20 * anchor)
    return result


def codec_residual_comb(u, v):
    row = min(3, round(v * 3.0))
    lag = (2, 4, 8, 16)[row]
    memory = lerp(0.14, 0.96, smoothstep(u))
    source = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        source.append(
            0.66 * math.sin(TAU * phase)
            + 0.22 * math.sin(TAU * phase * 3.0)
            + 0.12 * math.sin(TAU * phase * (11.0 + row * 2.0))
        )

    residual = []
    for sample in range(TABLE_SIZE):
        predictor = (
            source[(sample - lag) % TABLE_SIZE] * 0.74
            + source[(sample - lag * 2) % TABLE_SIZE] * 0.18
        )
        residual.append(source[sample] - predictor)
    residual_peak = max(abs(value) for value in residual)
    residual_scale = 1.0 / max(residual_peak, 1.0e-9)

    result = []
    for sample in range(TABLE_SIZE):
        comb = residual[sample] * residual_scale
        comb += residual[(sample - lag) % TABLE_SIZE] * residual_scale * memory * 0.72
        comb -= residual[(sample + lag) % TABLE_SIZE] * residual_scale * memory * 0.31
        shaped = math.tanh(comb * lerp(1.10, 2.8, memory))
        phase = sample / TABLE_SIZE
        anchor = math.sin(TAU * phase)
        result.append(lerp(source[sample], shaped, memory) + 0.20 * anchor)
    return result


def codec_residual_feedback(u, v):
    row = min(3, round(v * 3.0))
    lag = (1, 3, 7, 13)[row]
    feedback = lerp(0.16, 0.92, smoothstep(u))
    source = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        source.append(
            0.70 * math.sin(TAU * phase)
            + 0.20 * math.sin(TAU * phase * (3.0 + row * 2.0))
            + 0.10 * math.sin(TAU * phase * (13.0 + row * 4.0))
        )

    state = [0.0] * lag
    settled = [0.0] * TABLE_SIZE
    for cycle in range(12):
        for sample in range(TABLE_SIZE):
            delayed = state[sample % lag]
            predictor = source[(sample - lag) % TABLE_SIZE] * 0.68
            residual = source[sample] - predictor
            value = math.tanh(
                residual * lerp(1.15, 2.65, feedback)
                + delayed * feedback * 0.82
            )
            state[sample % lag] = value
            if cycle == 11:
                settled[sample] = value

    result = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        anchor = math.sin(TAU * phase)
        result.append(lerp(source[sample], settled[sample], feedback) + 0.20 * anchor)
    return result


def codec_cascade(u, v):
    row = min(3, round(v * 3.0))
    generations = 1 + round(smoothstep(u) * 7.0)
    block_size = (4, 8, 16, 32)[row]
    current = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        current.append(
            0.72 * math.sin(TAU * phase)
            + 0.18 * math.sin(TAU * phase * 3.0)
            + 0.10 * (2.0 * phase - 1.0)
        )

    for generation in range(generations):
        levels = max(3, 20 - generation * 2 - row)
        next_generation = [0.0] * TABLE_SIZE
        for sample in range(TABLE_SIZE):
            block = sample // block_size
            predicted = current[(sample - 1) % TABLE_SIZE] * lerp(0.74, 0.96, generation / 7.0)
            residual = clamp(current[sample] - predicted, -1.0, 1.0)
            quantized = round((residual * 0.5 + 0.5) * (levels - 1)) / (levels - 1) * 2.0 - 1.0
            reconstructed = predicted + quantized * lerp(0.92, 0.58, generation / 7.0)
            fault = hash01(block * 307 + generation * 1877 + row * 43) < (0.08 + 0.055 * generation)
            if fault:
                local_sample = sample % block_size
                stale_block = max(0, block - 1 - (generation & 1))
                stale_index = (stale_block * block_size + local_sample) % TABLE_SIZE
                reconstructed = lerp(reconstructed, current[stale_index], 0.68)
                if row >= 2 and ((block + generation) & 1):
                    reconstructed *= -1.0
            next_generation[sample] = math.tanh(reconstructed * (1.0 + generation * 0.16))
        current = next_generation

    result = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        result.append(current[sample] + 0.20 * math.sin(TAU * phase))
    return result


def cellular(u, v):
    row = min(3, round(v * 3.0))
    rules = (30, 45, 90, 150)
    width = 32
    state = [0] * width
    state[width // 2] = 1
    if row & 1:
        state[width // 2 - 3] = 1
    if row >= 2:
        state[width // 2 + 5] = 1

    depth = round(lerp(0.0, 30.0, smoothstep(u)))
    rule = rules[row]

    def advance(cells):
        next_cells = [0] * width
        for index in range(width):
            left = cells[(index - 1) % width]
            center = cells[index]
            right = cells[(index + 1) % width]
            neighborhood = (left << 2) | (center << 1) | right
            next_cells[index] = (rule >> neighborhood) & 1
        return next_cells

    for _ in range(depth):
        state = advance(state)
    data = []
    for _ in range(8):
        data.extend(1.0 if cell else -1.0 for cell in state)
        state = advance(state)

    result = []
    activity = smoothstep(u)
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        previous = data[(sample - 1) % TABLE_SIZE]
        following = data[(sample + 1) % TABLE_SIZE]
        softened = 0.25 * previous + 0.50 * data[sample] + 0.25 * following
        pattern = lerp(softened, data[sample], activity)
        anchor = math.sin(TAU * phase)
        result.append(lerp(anchor, pattern, 0.42 + activity * 0.52) + 0.20 * anchor)
    return result


def glitch_address(u, v):
    row = min(3, round(v * 3.0))
    damage = lerp(0.42, 0.98, smoothstep(u))
    blocks = [6, 9, 13, 16][row] + round(u * 12.0)
    result = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        block_phase = phase * blocks
        block = int(math.floor(block_phase))
        local = block_phase - block
        if row == 0:
            mapped = (local * lerp(1.0, 4.0, u)) % 1.0
        elif row == 1:
            mapped = local if block % 2 == 0 else 1.0 - local
        elif row == 2:
            mapped = (local + hash01(block * 41 + 7) * damage) % 1.0
        else:
            address = int(hash01(block * 173 + 19) * blocks) % blocks
            mapped = (address + local) / blocks
        carrier = 0.72 * math.sin(TAU * mapped) + 0.28 * math.sin(TAU * mapped * 3.0)
        anchor = math.sin(TAU * phase)
        result.append(lerp(anchor, carrier, damage) + 0.20 * anchor)
    return result


def glitch_pcm(u, v):
    row = min(3, round(v * 3.0))
    damage = lerp(0.48, 1.0, smoothstep(u))
    levels = max(2, round(lerp(10.0, 2.0, damage)))
    hold = max(1, round(lerp(2.0, 14.0, damage + 0.12 * row)))
    result = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        held = (sample // hold) * hold
        base = math.sin(TAU * phase) + 0.24 * math.sin(TAU * phase * (3 + row * 2))
        quantized = round((clamp(base * 0.72, -1.0, 1.0) * 0.5 + 0.5) * (levels - 1))
        quantized = quantized / (levels - 1) * 2.0 - 1.0
        data = hash01(held * 313 + row * 7919) * 2.0 - 1.0
        bit_gate = 1.0 if ((held // hold) >> row) & 1 else -1.0
        corrupt = lerp(quantized * bit_gate, data, 0.18 + 0.18 * row)
        anchor = math.sin(TAU * phase)
        result.append(lerp(anchor, corrupt, damage) + 0.20 * anchor)
    return result


def glitch_fracture(u, v):
    row = min(3, round(v * 3.0))
    damage = lerp(0.38, 0.96, smoothstep(u))
    blocks = 8 + row * 4 + round(u * 16.0)
    result = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        block = min(blocks - 1, int(phase * blocks))
        local = (phase * blocks) - block
        source = math.sin(TAU * phase) + 0.34 * math.sin(TAU * phase * (5 + row * 2))
        polarity = -1.0 if hash01(block * 101 + row * 17) < damage * 0.55 else 1.0
        dropout = 0.0 if hash01(block * 67 + row * 29) < damage * 0.24 else 1.0
        edge = 1.0 if local > lerp(0.03, 0.22, hash01(block * 43 + 3)) else -1.0
        fracture = source * polarity * dropout + edge * damage * (0.18 + 0.10 * row)
        anchor = math.sin(TAU * phase)
        result.append(lerp(anchor, fracture, damage) + 0.20 * anchor)
    return result


def vocal_pulse(phase, width):
    return 1.0 if (phase % 1.0) < clamp(width, 0.04, 0.96) else -1.0


def vocal_triangle(phase):
    return 4.0 * abs((phase % 1.0) - 0.5) - 1.0


def vocal_noise(seed):
    seed &= 0xFFFFFFFF
    seed ^= seed >> 16
    seed = (seed * 0x7FEB352D) & 0xFFFFFFFF
    seed ^= seed >> 15
    seed = (seed * 0x846CA68B) & 0xFFFFFFFF
    seed ^= seed >> 16
    return (seed & 0x00FFFFFF) / 0x00800000 - 1.0


def vocal_blackmetal(u, v):
    result = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        sine = math.sin(TAU * phase)
        throat = math.sin(TAU * phase * (2.0 + 5.0 * v)
                          + 0.55 * math.sin(TAU * phase * (3.0 + 9.0 * u)))
        shriek = math.sin(TAU * phase * (9.0 + 22.0 * u) + v * 2.1)
        scrape = vocal_noise(sample * 32 + int(u * 101.0) + int(v * 509.0))
        body = 0.36 * sine + 0.28 * vocal_pulse(phase, 0.18 + 0.34 * u) + 0.22 * throat
        result.append(math.tanh((body + (0.10 + 0.22 * u) * shriek
                                 + (0.06 + 0.18 * v) * scrape) * (1.35 + 3.25 * u)))
    return result


def vocal_throat(u, v):
    result = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        sub = math.sin(TAU * phase * 0.5 + v * 1.7)
        tone = 0.42 * math.sin(TAU * phase) + 0.28 * vocal_pulse(phase, 0.28 + 0.32 * u)
        form = 0.26 * sub + 0.18 * math.sin(TAU * phase * (3.0 + 5.0 * v))
        result.append(math.tanh((tone + form) * (1.0 + 2.2 * u)))
    return result


def vocal_choir(u, v):
    result = []
    f1 = 2.0 + 5.0 * v
    f2 = 5.0 + 10.0 * (1.0 - v)
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        voice = (0.62 * math.sin(TAU * phase)
                 + 0.22 * math.sin(TAU * phase * f1 + u * 0.4)
                 + 0.14 * math.sin(TAU * phase * f2 + v * 1.3))
        result.append(voice * (1.0 - 0.20 * u) + vocal_triangle(phase) * 0.20 * u)
    return result


def vocal_animal(u, v):
    result = []
    for sample in range(TABLE_SIZE):
        phase = sample / TABLE_SIZE
        folded = math.sin(TAU * (phase + 0.13 * math.sin(TAU * phase * (2.0 + 6.0 * u))))
        growl = math.sin(TAU * phase * (1.0 + 0.5 * v)) * vocal_pulse(
            phase * (2.0 + 3.0 * u), 0.34
        )
        result.append(math.tanh((0.48 * folded + 0.44 * growl
                                 + 0.16 * math.sin(TAU * phase)) * (1.2 + 1.8 * u)))
    return result


ATLASES = (
    ("foundation", "Foundation", "harmonic density", "odd/even balance", foundation),
    ("body-bass", "Body / Bass", "brightness", "body and asymmetry", body_bass),
    ("pulse-reed", "Pulse / Reed", "edge brightness", "pulse width", pulse_reed),
    ("formant", "Formant", "formant center", "bandwidth and vowel", formant),
    ("organ", "Organ", "registration", "drawbar family", organ),
    ("glass-metal", "Glass / Metal", "partial density", "harmonic lattice", glass_metal),
    ("fold-phase", "Fold / Phase", "fold intensity", "phase geometry", fold_phase),
    ("digital", "Digital", "sample resolution", "amplitude resolution", digital),
    ("string", "String", "excitation brightness", "pluck and bow balance", string),
    ("rungler", "Rungler", "feedback amount", "register tap and seed", rungler),
    ("barberpole", "Barberpole", "cyclic spectral center", "octave density", barberpole),
    ("harmonic-mirage", "Harmonic Mirage", "consonance to cluster", "registration and inversion", harmonic_mirage),
    ("wave-terrain", "Wave Terrain", "traversal angle", "terrain deformation", wave_terrain),
    ("codec-ghost", "Codec Ghost", "prediction failure", "packet block size", codec_ghost),
    ("codec-conceal", "Codec Conceal", "packet loss", "concealment method", codec_conceal),
    ("codec-residual", "Codec Residual", "residual emphasis", "predictor memory", codec_residual),
    ("codec-residual-delta", "Residual Delta", "difference emphasis", "difference order", codec_residual_delta),
    ("codec-residual-comb", "Residual Comb", "comb memory", "predictor lag", codec_residual_comb),
    ("codec-residual-feedback", "Residual Feedback", "feedback amount", "feedback lag", codec_residual_feedback),
    ("codec-cascade", "Codec Cascade", "transcode generations", "packet block size", codec_cascade),
    ("cellular", "Cellular", "generation depth", "rule family", cellular),
    ("glitch-address", "Glitch / Address", "address damage", "repeat and reorder mode", glitch_address),
    ("glitch-pcm", "Glitch / PCM", "data corruption", "bit pattern family", glitch_pcm),
    ("glitch-fracture", "Glitch / Fracture", "fracture depth", "polarity and dropout mode", glitch_fracture),
    ("vocal-blackmetal", "Vocal / Black Metal", "throat closure and distortion edge", "vowel darkness and rasp depth", vocal_blackmetal),
    ("vocal-throat", "Vocal / Throat", "closure", "subharmonic body", vocal_throat),
    ("vocal-choir", "Vocal / Choir", "strain", "vowel family", vocal_choir),
    ("vocal-animal", "Vocal / Animal", "snarl", "body size", vocal_animal),
)


ENERGY_SURFACES = {
    "rungler": (
        "feedback rise",
        lambda u, v: 0.28 + 0.72 * smoothstep(u),
    ),
    "wave-terrain": (
        "quiet center, loud perimeter",
        lambda u, v: 0.22 + 0.78 * (
            clamp(math.hypot(u - 0.5, v - 0.5) / math.sqrt(0.5), 0.0, 1.0) ** 1.65
        ),
    ),
    "codec-ghost": (
        "prediction-failure rise",
        lambda u, v: 0.30 + 0.70 * smoothstep(u),
    ),
    "codec-conceal": (
        "packet-loss rise",
        lambda u, v: 0.34 + 0.66 * smoothstep(u),
    ),
    "codec-residual": (
        "residual-pressure rise",
        lambda u, v: 0.36 + 0.64 * smoothstep(u),
    ),
    "codec-residual-delta": (
        "difference-order rise",
        lambda u, v: 0.34 + 0.66 * smoothstep(u),
    ),
    "codec-residual-comb": (
        "comb-memory rise",
        lambda u, v: 0.32 + 0.68 * smoothstep(u),
    ),
    "codec-residual-feedback": (
        "recursive-pressure rise",
        lambda u, v: 0.25 + 0.75 * smoothstep(u),
    ),
    "codec-cascade": (
        "generation-collapse rise",
        lambda u, v: 0.25 + 0.75 * smoothstep(u),
    ),
    "cellular": (
        "generation-activity rise",
        lambda u, v: 0.28 + 0.72 * smoothstep(u),
    ),
}


def make_atlas(generator, energy_surface=None):
    tables = []
    coordinates = []
    for row in range(GRID_SIZE):
        v = row / (GRID_SIZE - 1)
        for column in range(GRID_SIZE):
            u = column / (GRID_SIZE - 1)
            tables.append(generator(u, v))
            coordinates.append((u, v))
    tables = level_match_atlas(tables)
    if energy_surface is not None:
        for index, (u, v) in enumerate(coordinates):
            gain = clamp(energy_surface(u, v), 0.25, 1.0)
            tables[index] = [sample * gain for sample in tables[index]]
    return tables


def validate_atlas(name, tables):
    if len(tables) != TABLE_COUNT:
        raise RuntimeError(f"{name}: expected sixteen tables")
    for index, table in enumerate(tables):
        if len(table) != TABLE_SIZE or not all(math.isfinite(sample) for sample in table):
            raise RuntimeError(f"{name} table {index + 1}: invalid samples")
        peak = max(abs(sample) for sample in table)
        rms = math.sqrt(sum(sample * sample for sample in table) / TABLE_SIZE)
        fundamental_sine = 2.0 / TABLE_SIZE * sum(
            table[sample] * SINE_BASIS[1][sample] for sample in range(TABLE_SIZE)
        )
        fundamental_cosine = 2.0 / TABLE_SIZE * sum(
            table[sample] * COSINE_BASIS[1][sample] for sample in range(TABLE_SIZE)
        )
        fundamental = math.hypot(fundamental_sine, fundamental_cosine)
        if peak > 0.901 or rms < 0.04 or fundamental < 0.015:
            raise RuntimeError(
                f"{name} table {index + 1}: peak={peak:.3f}, rms={rms:.3f}, fundamental={fundamental:.3f}"
            )


def pcm24_bytes(samples):
    output = bytearray()
    for sample in samples:
        value = int(round(clamp(sample, -1.0, 1.0) * 8388607.0))
        if value < 0:
            value += 1 << 24
        output.extend((value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF))
    return bytes(output)


def write_wav(path, tables):
    samples = [sample for table in tables for sample in table]
    if len(samples) != TABLE_COUNT * TABLE_SIZE:
        raise RuntimeError("VOT atlas must contain exactly 4096 samples")
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(3)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(pcm24_bytes(samples))


def svg_path(table, x, y, width, height):
    points = []
    for sample in range(0, TABLE_SIZE, 2):
        px = x + sample / (TABLE_SIZE - 2) * width
        py = y + height * 0.5 - table[sample] * height * 0.42
        command = "M" if not points else "L"
        points.append(f"{command}{px:.2f},{py:.2f}")
    return " ".join(points)


def write_preview(path, title, u_label, v_label, energy_label, tables):
    width = 760
    height = 760
    left = 76
    top = 72
    cell = 152
    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="760" height="760" fill="#161616"/>',
        f'<text x="24" y="32" fill="#dedede" font-family="monospace" font-size="15">S3G VOT / {title.upper()}</text>',
        f'<text x="684" y="724" text-anchor="end" fill="#858585" font-family="monospace" font-size="11">U / {u_label.upper()}</text>',
        f'<text x="24" y="72" fill="#858585" font-family="monospace" font-size="11">V / {v_label.upper()}</text>',
        f'<text x="76" y="724" fill="#858585" font-family="monospace" font-size="11">ENERGY / {energy_label.upper()}</text>',
    ]
    for row in range(GRID_SIZE):
        for column in range(GRID_SIZE):
            table_index = row * GRID_SIZE + column
            x = left + column * cell
            y = top + row * cell
            lines.append(f'<rect x="{x}" y="{y}" width="{cell}" height="{cell}" fill="#0b0b0b" stroke="#444"/>')
            lines.append(f'<line x1="{x + 8}" y1="{y + cell / 2:.2f}" x2="{x + cell - 8}" y2="{y + cell / 2:.2f}" stroke="#292929"/>')
            lines.append(f'<path d="{svg_path(tables[table_index], x + 8, y + 18, cell - 16, cell - 36)}" fill="none" stroke="#e0e0e0" stroke-width="1.4"/>')
            lines.append(f'<text x="{x + 7}" y="{y + 14}" fill="#666" font-family="monospace" font-size="9">{table_index + 1:02d}</text>')
    lines.append('</svg>')
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def generate(output_directory):
    output_directory.mkdir(parents=True, exist_ok=True)
    preview_directory = output_directory / "previews"
    preview_directory.mkdir(parents=True, exist_ok=True)
    manifest = {
        "format": "s3g-vot-atlas-v1",
        "sample_rate": SAMPLE_RATE,
        "channels": 1,
        "sample_format": "PCM 24-bit",
        "grid": [GRID_SIZE, GRID_SIZE],
        "table_size": TABLE_SIZE,
        "table_order": "row-major, top-left to bottom-right",
        "atlases": [],
    }
    for slug, title, u_label, v_label, generator in ATLASES:
        energy_surface = ENERGY_SURFACES.get(slug)
        tables = make_atlas(generator, energy_surface[1] if energy_surface else None)
        validate_atlas(slug, tables)
        table_rms = [math.sqrt(sum(sample * sample for sample in table) / TABLE_SIZE) for table in tables]
        energy_range_db = 20.0 * math.log10(max(table_rms) / max(1.0e-9, min(table_rms)))
        wav_name = f"s3g-vot-{slug}.wav"
        preview_name = f"s3g-vot-{slug}.svg"
        write_wav(output_directory / wav_name, tables)
        write_preview(
            preview_directory / preview_name,
            title,
            u_label,
            v_label,
            energy_surface[0] if energy_surface else "level matched",
            tables,
        )
        manifest["atlases"].append({
            "id": slug,
            "name": title,
            "u_axis": u_label,
            "v_axis": v_label,
            "energy_surface": energy_surface[0] if energy_surface else "level matched",
            "energy_range_db": round(energy_range_db, 2),
            "wav": wav_name,
            "preview": f"previews/{preview_name}",
        })
        print(output_directory / wav_name)
    (output_directory / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="ascii")


def main():
    parser = argparse.ArgumentParser(description="Generate the s3g VOT 4 x 4 wavetable atlas library")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "wavetables" / "vot",
        help="output directory (default: repo wavetables/vot)",
    )
    arguments = parser.parse_args()
    generate(arguments.output)


if __name__ == "__main__":
    main()
