CREATE EXTENSION IF NOT EXISTS pgcrypto;

CREATE TABLE IF NOT EXISTS student_group (
    id          BIGSERIAL PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    size        INTEGER NOT NULL CHECK (size > 0),

    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS teacher (
    id          BIGSERIAL PRIMARY KEY,
    full_name   TEXT NOT NULL,

    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS subject (
    id          BIGSERIAL PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    difficulty  INTEGER NOT NULL CHECK (difficulty BETWEEN 1 AND 5),

    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS teacher_subject (
    teacher_id  BIGINT NOT NULL REFERENCES teacher(id) ON DELETE CASCADE,
    subject_id  BIGINT NOT NULL REFERENCES subject(id) ON DELETE CASCADE,

    PRIMARY KEY (teacher_id, subject_id)
);

CREATE TABLE IF NOT EXISTS room (
    id          BIGSERIAL PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    capacity    INTEGER NOT NULL CHECK (capacity > 0),

    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS timeslot (
    id              BIGSERIAL PRIMARY KEY,
    slot_date       DATE NOT NULL,
    start_minutes   INTEGER NOT NULL CHECK (start_minutes >= 0 AND start_minutes < 24*60),
    end_minutes     INTEGER NOT NULL CHECK (end_minutes > start_minutes AND end_minutes <= 24*60),

    CONSTRAINT timeslot_unique_per_day UNIQUE (slot_date, start_minutes, end_minutes),

    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS exam (
    id                  BIGSERIAL PRIMARY KEY,
    group_id            BIGINT NOT NULL REFERENCES student_group(id) ON DELETE RESTRICT,
    teacher_id          BIGINT NOT NULL REFERENCES teacher(id) ON DELETE RESTRICT,
    subject_id          BIGINT NOT NULL REFERENCES subject(id) ON DELETE RESTRICT,
    duration_minutes    INTEGER NOT NULL CHECK (duration_minutes > 0),

    created_at          TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at          TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS exam_assignment (
    id              BIGSERIAL PRIMARY KEY,
    exam_id         BIGINT NOT NULL REFERENCES exam(id) ON DELETE CASCADE,
    timeslot_id     BIGINT NOT NULL REFERENCES timeslot(id) ON DELETE RESTRICT,
    room_id         BIGINT NOT NULL REFERENCES room(id) ON DELETE RESTRICT,

    CONSTRAINT exam_assignment_unique_exam UNIQUE (exam_id),

    CONSTRAINT exam_assignment_unique_slot_room UNIQUE (timeslot_id, room_id),

    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'user_role') THEN
        CREATE TYPE user_role AS ENUM ('admin', 'methodist', 'teacher', 'student');
    END IF;
END$$;

CREATE TABLE IF NOT EXISTS app_user (
    id              BIGSERIAL PRIMARY KEY,

    username        TEXT NOT NULL UNIQUE,  
    password_hash   TEXT NOT NULL,            

    role            user_role NOT NULL DEFAULT 'methodist',

    email_plain     TEXT,
    email_encrypted BYTEA,

    created_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS exam_schedule (
    id          BIGSERIAL PRIMARY KEY,

    user_id     BIGINT NOT NULL REFERENCES app_user(id) ON DELETE CASCADE,

    -- config, который пришёл с фронта (groups, teachers, rooms, subjects, exams, session)
    config_json JSONB NOT NULL,

    -- результат генерации (то, что сейчас бэкенд возвращает фронту)
    result_json JSONB NOT NULL,

    created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS idx_exam_schedule_user_id
    ON exam_schedule(user_id);
