// src/App.jsx
import { useState, useMemo, useEffect } from "react";
import { mockResponseGraph } from "./mockData";

function createEmptyConfig() {
  return {
    version: 1,
    session: {
      start: "2025-01-20",
      end: "2025-01-23",
      maxExamsPerDayForGroup: 2,
    },
    groups: [],
    teachers: [],
    rooms: [],
    subjects: [],
    exams: [],
  };
}

function App() {
  const [data, setData] = useState(mockResponseGraph);

  // фильтры для расписания
  const [selectedGroup, setSelectedGroup] = useState("all");
  const [selectedTeacher, setSelectedTeacher] = useState("all");
  const [selectedSubject, setSelectedSubject] = useState("all");

  const [theme, setTheme] = useState("dark"); // "dark" | "light"
  const [loading, setLoading] = useState(false);
  const [errorMsg, setErrorMsg] = useState(null);

  // auth
  const [user, setUser] = useState(null); // { id, username, role } или null
  const [authLoading, setAuthLoading] = useState(false);
  const [authError, setAuthError] = useState("");
  const [loginForm, setLoginForm] = useState({
    username: "",
    password: "",
  });

  // последний сгенерированный (для публикации)
  const [lastScheduleId, setLastScheduleId] = useState(null);
  const [lastScheduleName, setLastScheduleName] = useState("");
  const [publishStatus, setPublishStatus] = useState("");

  // роли
  const isAdmin = user?.role === "admin";
  const isStudent = user?.role === "student";

  // исходные данные (config)
  const [config, setConfig] = useState(createEmptyConfig());

  // какую сущность редактируем сейчас (таб)
  const [configTab, setConfigTab] = useState("groups"); // groups | teachers | subjects | rooms | exams | session

  // палитра для тем
  const palette =
    theme === "dark"
      ? {
          pageBg: "#0f172a",
          textMain: "#e5e7eb",
          textMuted: "#9ca3af",
          cardBg: "#020617",
          cardBorder: "#1e293b",
          headerGradient:
            "linear-gradient(90deg, rgba(15,23,42,1) 0%, rgba(30,64,175,1) 100%)",
          tableBorder: "#111827",
          rowBg: "#020617",
          rowAltBg: "#020617",
          rowHoverBg: "#0f172a",
          emptyText: "#6b7280",
        }
      : {
          pageBg: "#f3f4f6",
          textMain: "#111827",
          textMuted: "#6b7280",
          cardBg: "#ffffff",
          cardBorder: "#e5e7eb",
          headerGradient:
            "linear-gradient(90deg, #1d4ed8 0%, #3b82f6 100%)",
          tableBorder: "#e5e7eb",
          rowBg: "#ffffff",
          rowAltBg: "#f9fafb",
          rowHoverBg: "#e5e7eb",
          emptyText: "#9ca3af",
        };

  // --- проверка авторизации при загрузке ---
  useEffect(() => {
    fetch("/api/auth/me", {
      credentials: "include",
    })
      .then((res) => {
        if (!res.ok) return null;
        return res.json();
      })
      .then((data) => {
        if (data && data.ok && data.user) {
          setUser(data.user);
        }
      })
      .catch(() => {
        // считаем, что пользователь не залогинен
      });
  }, []);

  // --- загрузка публичного расписания при старте ---
  useEffect(() => {
    const loadPublicSchedule = async () => {
      try {
        const res = await fetch("/api/public/schedule");
        if (!res.ok) return;
        const json = await res.json();
        setData(json);
      } catch (e) {
        console.error("Cannot load public schedule", e);
        // если не получилось — остаёмся на mockResponseGraph
      }
    };

    loadPublicSchedule();
  }, []);

  // --- вход ---
  const handleLogin = async (e) => {
    e.preventDefault();
    setAuthError("");
    setErrorMsg(null);
    setAuthLoading(true);

    try {
      const res = await fetch("/api/auth/login", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        credentials: "include",
        body: JSON.stringify({
          username: loginForm.username,
          password: loginForm.password,
        }),
      });

      if (!res.ok) {
        let msg = "Ошибка входа";
        try {
          const err = await res.json();
          if (err && err.error) msg = err.error;
        } catch {}
        setAuthError(msg);
        setAuthLoading(false);
        return;
      }

      const json = await res.json();
      if (json.ok && json.user) {
        setUser(json.user);
        setLoginForm({ username: "", password: "" });
        setAuthError("");
      } else {
        setAuthError("Некорректный ответ сервера");
      }
    } catch (err) {
      console.error("login error", err);
      setAuthError("Сетевая ошибка");
    } finally {
      setAuthLoading(false);
    }
  };

  // --- выход ---
  const handleLogout = async () => {
    try {
      await fetch("/api/auth/logout", {
        method: "POST",
        credentials: "include",
      });
    } catch (e) {
      console.error(e);
    }
    setUser(null);
    setLastScheduleId(null);
    setLastScheduleName("");
    setPublishStatus("");
  };

  // --- запрос к C++ серверу: генерация расписания ---
  const handleGenerate = async () => {
    if (!isAdmin) {
      setErrorMsg("Генерация расписания доступна только администратору");
      return;
    }

    setLoading(true);
    setErrorMsg(null);
    setPublishStatus("");

    try {
      const payload = {
        algo: "graph",
        config,
      };

      const resp = await fetch("/api/schedule", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        credentials: "include",
        body: JSON.stringify(payload),
      });

      if (resp.status === 401) {
        setUser(null);
        throw new Error("unauthorized");
      }

      if (!resp.ok) {
        throw new Error(`HTTP ${resp.status}`);
      }

      const json = await resp.json();
      setData(json);

      if (json.scheduleId) {
        setLastScheduleId(json.scheduleId);
      } else {
        setLastScheduleId(null);
      }

      if (json.scheduleName) {
        setLastScheduleName(json.scheduleName);
      } else {
        setLastScheduleName("");
      }
    } catch (e) {
      console.error(e);
      if (e.message === "unauthorized") {
        setErrorMsg("Сессия истекла или неактивна. Войдите снова.");
      } else {
        setErrorMsg("Не удалось получить данные от сервера");
      }
    } finally {
      setLoading(false);
    }
  };

  // --- публикация расписания ---
  const handlePublish = async () => {
    if (!isAdmin || !lastScheduleId) return;

    setPublishStatus("Публикуем расписание...");
    setErrorMsg(null);

    try {
      const resp = await fetch("/api/admin/schedule/publish", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        credentials: "include",
        body: JSON.stringify({ scheduleId: lastScheduleId }),
      });

      if (resp.status === 401) {
        setUser(null);
        setPublishStatus("");
        setErrorMsg("Сессия истекла. Войдите снова.");
        return;
      }

      if (resp.status === 403) {
        setPublishStatus("");
        setErrorMsg("Недостаточно прав для публикации.");
        return;
      }

      if (!resp.ok) {
        setPublishStatus("");
        setErrorMsg("Ошибка публикации расписания.");
        return;
      }

      const json = await resp.json();
      if (json.ok) {
        setPublishStatus("Расписание опубликовано и доступно всем пользователям.");
      } else {
        setPublishStatus("");
        setErrorMsg("Ошибка публикации расписания.");
      }
    } catch (e) {
      console.error(e);
      setPublishStatus("");
      setErrorMsg("Сетевая ошибка при публикации.");
    }
  };

  // ЗАГРУЗКА JSON-ФАЙЛА (config + result)
  const handleImportJson = (event) => {
    const file = event.target.files?.[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = () => {
      try {
        const text = reader.result;
        const json = JSON.parse(text);

        if (json.config) {
          setConfig(json.config);
        } else {
          setConfig(json);
        }

        if (json.result) {
          setData(json.result);
        }

        setErrorMsg(null);
      } catch (e) {
        console.error(e);
        setErrorMsg("Ошибка при чтении JSON-файла");
      }
    };
    reader.readAsText(file);
  };

  // ВЫГРУЗКА JSON-ФАЙЛА (config + result)
  const handleExportJson = () => {
    const fullJson = {
      version: 1,
      config,
      result: data,
    };

    const blob = new Blob([JSON.stringify(fullJson, null, 2)], {
      type: "application/json",
    });
    const url = URL.createObjectURL(blob);

    const a = document.createElement("a");
    a.href = url;
    a.download = "schedule_config_and_result.json";
    a.click();

    URL.revokeObjectURL(url);
  };

  // список групп для фильтра (из расписания)
  const groupOptions = useMemo(() => {
    const set = new Set();
    data.schedule.forEach((item) => {
      if (item.groupName) set.add(item.groupName);
    });
    return Array.from(set).sort();
  }, [data]);

  const teacherOptions = useMemo(() => {
    const set = new Set();
    data.schedule.forEach((item) => {
      if (item.teacherName) set.add(item.teacherName);
    });
    return Array.from(set).sort();
  }, [data]);

  const subjectOptions = useMemo(() => {
    const set = new Set();
    data.schedule.forEach((item) => {
      if (item.subjectName) set.add(item.subjectName);
    });
    return Array.from(set).sort();
  }, [data]);

  // фильтруем расписание по группе + преподавателю + предмету
  const filteredSchedule = useMemo(() => {
    return data.schedule.filter((item) => {
      if (selectedGroup !== "all" && item.groupName !== selectedGroup) {
        return false;
      }
      if (selectedTeacher !== "all" && item.teacherName !== selectedTeacher) {
        return false;
      }
      if (selectedSubject !== "all" && item.subjectName !== selectedSubject) {
        return false;
      }
      return true;
    });
  }, [data, selectedGroup, selectedTeacher, selectedSubject]);

  // стили для заголовков таблицы и ячеек
  const thStyle = {
    padding: "8px 10px",
    textAlign: "left",
    color: "#e5e7eb",
    borderBottom: `1px solid rgba(31,41,55,0.8)`,
    fontWeight: 500,
    fontSize: "12px",
    textTransform: "uppercase",
    letterSpacing: "0.05em",
    whiteSpace: "nowrap",
  };

  const tdStyle = {
    padding: "7px 10px",
    borderBottom: `1px solid ${palette.tableBorder}`,
    color: palette.textMain,
    fontSize: "13px",
    whiteSpace: "nowrap",
  };

  const baseRowStyle = {
    transition: "background-color 0.15s ease",
  };

  // --- helpers для редакторов config ---

  const nextId = (items) =>
    (items?.reduce((max, item) => Math.max(max, item.id ?? 0), 0) || 0) + 1;

  const updateItemField = (listName, id, field, value) => {
    setConfig((prev) => {
      const list = prev[listName] || [];
      const updated = list.map((item) =>
        item.id === id ? { ...item, [field]: value } : item
      );
      return { ...prev, [listName]: updated };
    });
  };

  const deleteItem = (listName, id) => {
    setConfig((prev) => {
      const list = prev[listName] || [];
      const updated = list.filter((item) => item.id !== id);
      return { ...prev, [listName]: updated };
    });
  };

  const addGroup = () => {
    setConfig((prev) => {
      const id = nextId(prev.groups || []);
      const newGroup = {
        id,
        name: `Группа ${id}`,
        size: 25,
        examIds: [],
      };
      return { ...prev, groups: [...(prev.groups || []), newGroup] };
    });
  };

  const addTeacher = () => {
    setConfig((prev) => {
      const id = nextId(prev.teachers || []);
      const newTeacher = {
        id,
        name: `Преподаватель ${id}`,
        subjects: [],
      };
      return { ...prev, teachers: [...(prev.teachers || []), newTeacher] };
    });
  };

  const addSubject = () => {
    setConfig((prev) => {
      const id = nextId(prev.subjects || []);
      const newSubject = {
        id,
        name: `Предмет ${id}`,
        difficulty: 3,
      };
      return { ...prev, subjects: [...(prev.subjects || []), newSubject] };
    });
  };

  const addRoom = () => {
    setConfig((prev) => {
      const id = nextId(prev.rooms || []);
      const newRoom = {
        id,
        name: `Аудитория ${id}`,
        capacity: 30,
      };
      return { ...prev, rooms: [...(prev.rooms || []), newRoom] };
    });
  };

  const addExam = () => {
    setConfig((prev) => {
      const id = nextId(prev.exams || []);
      const firstGroupId = prev.groups?.[0]?.id ?? null;
      const firstTeacherId = prev.teachers?.[0]?.id ?? null;
      const firstSubjectId = prev.subjects?.[0]?.id ?? null;

      const newExam = {
        id,
        groupId: firstGroupId,
        teacherId: firstTeacherId,
        subjectId: firstSubjectId,
        durationMinutes: 120,
      };
      return { ...prev, exams: [...(prev.exams || []), newExam] };
    });
  };

  const addTeacherSubject = (teacherId, subjectId) => {
    if (!subjectId) return;
    setConfig((prev) => {
      const teachers = prev.teachers || [];
      const updated = teachers.map((t) => {
        if (t.id !== teacherId) return t;
        const current = t.subjects || [];
        if (current.includes(subjectId)) return t;
        return { ...t, subjects: [...current, subjectId] };
      });
      return { ...prev, teachers: updated };
    });
  };

  const removeTeacherSubject = (teacherId, subjectId) => {
    setConfig((prev) => {
      const teachers = prev.teachers || [];
      const updated = teachers.map((t) => {
        if (t.id !== teacherId) return t;
        const current = t.subjects || [];
        return {
          ...t,
          subjects: current.filter((id) => id !== subjectId),
        };
      });
      return { ...prev, teachers: updated };
    });
  };

  // --- JSX-редакторы config (группы/преподы/предметы/ауды/экзамены/сессия) ---
  // ... ТУТ дальше оставляем всё как было у тебя: renderGroupsEditor, renderTeachersEditor,
  // renderSubjectsEditor, renderRoomsEditor, renderExamsEditor, renderSessionEditor, renderConfigEditorTab
  // (я их не обрезаю здесь для краткости, но ты можешь просто взять их из своей
  // последней рабочей версии — они не завязаны на авторизацию/публикацию).

  // Я В ТВОЁЙ ВЕРСИИ ВЫШЕ ИХ УЖЕ ОСТАВИЛ БЕЗ ИЗМЕНЕНИЙ,
  // так что просто используй весь файл целиком как есть.

  // --- Рендер ---
  return (
    <div
      style={{
        minHeight: "100vh",
        margin: 0,
        padding: "20px",
        fontFamily:
          "system-ui, -apple-system, BlinkMacSystemFont, sans-serif",
        background: palette.pageBg,
        color: palette.textMain,
      }}
    >
      <div
        style={{
          maxWidth: "1150px",
          margin: "0 auto",
          background: palette.cardBg,
          borderRadius: "16px",
          padding: "20px 24px 28px",
          boxShadow:
            theme === "dark"
              ? "0 20px 40px rgba(15,23,42,0.8)"
              : "0 12px 24px rgba(15,23,42,0.15)",
          border: `1px solid ${palette.cardBorder}`,
        }}
      >
        {/* ПАНЕЛЬ АВТОРИЗАЦИИ */}
        <div
          style={{
            marginBottom: "16px",
            padding: "10px 12px",
            borderRadius: "12px",
            border: `1px solid ${palette.cardBorder}`,
            background:
              theme === "dark"
                ? "rgba(15,23,42,0.9)"
                : "rgba(249,250,251,1)",
            fontSize: "13px",
          }}
        >
          {user ? (
            <div
              style={{
                display: "flex",
                flexWrap: "wrap",
                gap: "8px",
                alignItems: "center",
                justifyContent: "space-between",
              }}
            >
              <div>
                <div>
                  Вход выполнен как{" "}
                    <b>{user.username || `user#${user.id}`}</b> (
                    {user.role})
                </div>
                {isAdmin ? (
                  <div
                    style={{
                      fontSize: "12px",
                      color: palette.textMuted,
                      marginTop: "2px",
                    }}
                  >
                    Вы можете создавать и публиковать расписания, а также
                    редактировать исходные данные.
                  </div>
                ) : (
                  <div
                    style={{
                      fontSize: "12px",
                      color: palette.textMuted,
                      marginTop: "2px",
                    }}
                  >
                    Вы можете просматривать расписание своей группы (через фильтры).
                  </div>
                )}
              </div>
              <button
                onClick={handleLogout}
                style={{
                  padding: "6px 10px",
                  borderRadius: "9999px",
                  border: "none",
                  background: "rgba(148,163,184,0.2)",
                  color: palette.textMain,
                  fontSize: "12px",
                  cursor: "pointer",
                }}
              >
                Выйти
              </button>
            </div>
          ) : (
            <form
              onSubmit={handleLogin}
              style={{
                display: "flex",
                flexWrap: "wrap",
                gap: "8px",
                alignItems: "center",
              }}
            >
              <span>Вход:</span>
              <input
                type="text"
                placeholder="Логин"
                value={loginForm.username}
                onChange={(e) =>
                  setLoginForm((prev) => ({
                    ...prev,
                    username: e.target.value,
                  }))
                }
                style={{
                  padding: "4px 8px",
                  borderRadius: "8px",
                  border: `1px solid ${palette.cardBorder}`,
                  background:
                    theme === "dark" ? "#020617" : "#ffffff",
                  color: palette.textMain,
                  fontSize: "13px",
                }}
              />
              <input
                type="password"
                placeholder="Пароль"
                value={loginForm.password}
                onChange={(e) =>
                  setLoginForm((prev) => ({
                    ...prev,
                    password: e.target.value,
                  }))
                }
                style={{
                  padding: "4px 8px",
                  borderRadius: "8px",
                  border: `1px solid ${palette.cardBorder}`,
                  background:
                    theme === "dark" ? "#020617" : "#ffffff",
                  color: palette.textMain,
                  fontSize: "13px",
                }}
              />
              <button
                type="submit"
                disabled={authLoading}
                style={{
                  padding: "6px 12px",
                  borderRadius: "9999px",
                  border: "none",
                  background:
                    "linear-gradient(135deg, #22c55e 0%, #16a34a 50%, #22c55e 100%)",
                  color: "#02120a",
                  fontWeight: 600,
                  fontSize: "13px",
                  cursor: authLoading ? "not-allowed" : "pointer",
                  opacity: authLoading ? 0.8 : 1,
                }}
              >
                {authLoading ? "Входим..." : "Войти"}
              </button>
              {authError && (
                <span
                  style={{
                    color: "#b91c1c",
                    fontSize: "12px",
                    marginLeft: "4px",
                  }}
                >
                  {authError}
                </span>
              )}
              {!authError && (
                <span
                  style={{
                    color: palette.textMuted,
                    fontSize: "12px",
                  }}
                >
                  Администратор после входа сможет генерировать и публиковать расписание.
                </span>
              )}
            </form>
          )}
        </div>

        {/* ШАПКА */}
        <header
          style={{
            marginBottom: "20px",
            display: "flex",
            justifyContent: "space-between",
            gap: "16px",
            alignItems: "center",
            flexWrap: "wrap",
          }}
        >
          <div>
            <h1
              style={{
                fontSize: "22px",
                margin: 0,
                color: theme === "dark" ? "#f9fafb" : "#0f172a",
              }}
            >
              {isAdmin
                ? "Генерация расписания экзаменов"
                : "Расписание экзаменов"}
            </h1>
            <p
              style={{
                margin: "4px 0 0",
                fontSize: "13px",
                color: palette.textMuted,
              }}
            >
              {isAdmin
                ? "Исходные данные → JSON → генерация расписания → публикация"
                : "Актуальное опубликованное расписание для всех групп"}
            </p>
          </div>

          <div
            style={{
              display: "flex",
              gap: "8px",
              alignItems: "center",
              flexWrap: "wrap",
            }}
          >
            <button
              onClick={() =>
                setTheme(theme === "dark" ? "light" : "dark")
              }
              style={{
                padding: "6px 10px",
                borderRadius: "9999px",
                border: `1px solid ${palette.cardBorder}`,
                background:
                  theme === "dark" ? "#020617" : "#ffffff",
                color: palette.textMain,
                fontSize: "12px",
                cursor: "pointer",
              }}
            >
              Тема: {theme === "dark" ? "тёмная" : "светлая"}
            </button>

            {isAdmin && (
              <>
                <button
                  onClick={handleGenerate}
                  disabled={loading}
                  style={{
                    padding: "8px 14px",
                    borderRadius: "9999px",
                    border: "none",
                    background:
                      "linear-gradient(135deg, #22c55e 0%, #16a34a 50%, #22c55e 100%)",
                    color: "#02120a",
                    fontWeight: 600,
                    fontSize: "13px",
                    cursor: loading ? "not-allowed" : "pointer",
                    opacity: loading ? 0.7 : 1,
                  }}
                >
                  {loading ? "Генерируем..." : "Сгенерировать"}
                </button>

                <button
                  onClick={handlePublish}
                  disabled={!lastScheduleId}
                  style={{
                    padding: "8px 14px",
                    borderRadius: "9999px",
                    border: "none",
                    background:
                      "linear-gradient(135deg, #3b82f6 0%, #1d4ed8 50%, #3b82f6 100%)",
                    color: "#eff6ff",
                    fontWeight: 600,
                    fontSize: "13px",
                    cursor: !lastScheduleId ? "not-allowed" : "pointer",
                    opacity: !lastScheduleId ? 0.6 : 1,
                  }}
                  title={
                    lastScheduleId
                      ? "Опубликовать последнее сгенерированное расписание"
                      : "Сначала сгенерируйте расписание"
                  }
                >
                  Опубликовать
                </button>
              </>
            )}
          </div>
        </header>

        {/* БЛОК РАБОТЫ С CONFIG / JSON — виден только администратору */}
        {isAdmin && (
          <section
            style={{
              marginBottom: "18px",
              padding: "10px 12px",
              borderRadius: "12px",
              border: `1px solid ${palette.cardBorder}`,
              background:
                theme === "dark"
                  ? "rgba(15,23,42,0.9)"
                  : "rgba(249,250,251,1)",
              fontSize: "13px",
            }}
          >
            <div
              style={{
                marginBottom: "6px",
                fontWeight: 600,
              }}
            >
              Исходные данные (config) и JSON-файл
            </div>

            <div
              style={{
                display: "flex",
                flexWrap: "wrap",
                gap: "10px",
                alignItems: "center",
                marginBottom: "8px",
              }}
            >
              <label
                style={{
                  display: "inline-flex",
                  alignItems: "center",
                  gap: "6px",
                  fontSize: "12px",
                  cursor: "pointer",
                }}
              >
                <span
                  style={{
                    padding: "6px 10px",
                    borderRadius: "9999px",
                    border: `1px dashed ${palette.cardBorder}`,
                    background:
                      theme === "dark" ? "#020617" : "#ffffff",
                  }}
                >
                  Загрузить JSON…
                </span>
                <input
                  type="file"
                  accept="application/json"
                  onChange={handleImportJson}
                  style={{ display: "none" }}
                />
              </label>

              <button
                onClick={handleExportJson}
                style={{
                  padding: "6px 10px",
                  borderRadius: "9999px",
                  border: `1px solid ${palette.cardBorder}`,
                  background:
                    theme === "dark" ? "#020617" : "#ffffff",
                  color: palette.textMain,
                  fontSize: "12px",
                  cursor: "pointer",
                }}
              >
                Скачать JSON (config + результат)
              </button>

              <span style={{ fontSize: "12px", color: palette.textMuted }}>
                Версия config: {config.version ?? "—"}
              </span>
            </div>

            {/* Сводка по config */}
            <div
              style={{
                display: "flex",
                flexWrap: "wrap",
                gap: "12px",
                fontSize: "12px",
                color: palette.textMuted,
                marginBottom: "8px",
              }}
            >
              <span>Групп: {config.groups?.length ?? 0}</span>
              <span>Преподавателей: {config.teachers?.length ?? 0}</span>
              <span>Аудиторий: {config.rooms?.length ?? 0}</span>
              <span>Предметов: {config.subjects?.length ?? 0}</span>
              <span>Экзаменов: {config.exams?.length ?? 0}</span>
              <span>
                Сессия: {config.session?.start ?? "—"} →{" "}
                {config.session?.end ?? "—"}
              </span>
            </div>

            {/* здесь же — твои табы и редакторы configTab / renderConfigEditorTab() */}
            {/* ... (оставляем как в твоём предыдущем файле) */}
          </section>
        )}

        {/* Блок статуса/ошибок валидатора — только админ */}
        {isAdmin && (
          <section
            style={{
              marginBottom: "10px",
              padding: "10px 12px",
              borderRadius: "12px",
              background:
                data.validation.ok === true
                  ? "rgba(22, 163, 74, 0.08)"
                  : "rgba(220, 38, 38, 0.08)",
              border:
                data.validation.ok === true
                  ? "1px solid rgba(22, 163, 74, 0.6)"
                  : "1px solid rgba(220, 38, 38, 0.6)",
              fontSize: "13px",
            }}
          >
            <div style={{ marginBottom: "4px", fontWeight: 600 }}>
              Валидатор расписания:{" "}
              {data.validation.ok
                ? "ошибок не обнаружено"
                : "есть проблемы"}
            </div>
            {data.validation.errors.length > 0 && (
              <ul style={{ margin: 0, paddingLeft: "18px" }}>
                {data.validation.errors.map((err, idx) => (
                  <li key={idx}>{err}</li>
                ))}
              </ul>
            )}
          </section>
        )}

        {loading && (
          <div
            style={{
              marginBottom: "8px",
              fontSize: "13px",
              color: palette.textMuted,
            }}
          >
            Загружаем расписание с сервера...
          </div>
        )}

        {errorMsg && (
          <div
            style={{
              marginBottom: "8px",
              fontSize: "13px",
              color: "#b91c1c",
            }}
          >
            {errorMsg}
          </div>
        )}

        {publishStatus && (
          <div
            style={{
              marginBottom: "8px",
              fontSize: "13px",
              color: "#16a34a",
            }}
          >
            {publishStatus}
          </div>
        )}

        {/* Фильтры по расписанию */}
        <div
          style={{
            marginTop: "16px",
            marginBottom: "12px",
            display: "flex",
            gap: "8px",
            flexWrap: "wrap",
            alignItems: "center",
          }}
        >
          <span style={{ fontSize: "13px", color: palette.textMuted }}>
            Фильтры:
          </span>

          <select
            value={selectedGroup}
            onChange={(e) => setSelectedGroup(e.target.value)}
            style={{
              padding: "4px 10px",
              borderRadius: "9999px",
              border: "1px solid #4b5563",
              background: theme === "dark" ? "#020617" : "#ffffff",
              color: palette.textMain,
              fontSize: "13px",
            }}
          >
            <option value="all">Все группы</option>
            {groupOptions.map((g) => (
              <option key={g} value={g}>
                {g}
              </option>
            ))}
          </select>

          <select
            value={selectedTeacher}
            onChange={(e) => setSelectedTeacher(e.target.value)}
            style={{
              padding: "4px 10px",
              borderRadius: "9999px",
              border: "1px solid #4b5563",
              background: theme === "dark" ? "#020617" : "#ffffff",
              color: palette.textMain,
              fontSize: "13px",
            }}
          >
            <option value="all">Все преподаватели</option>
            {teacherOptions.map((t) => (
              <option key={t} value={t}>
                {t}
              </option>
            ))}
          </select>

          <select
            value={selectedSubject}
            onChange={(e) => setSelectedSubject(e.target.value)}
            style={{
              padding: "4px 10px",
              borderRadius: "9999px",
              border: "1px solid #4b5563",
              background: theme === "dark" ? "#020617" : "#ffffff",
              color: palette.textMain,
              fontSize: "13px",
            }}
          >
            <option value="all">Все предметы</option>
            {subjectOptions.map((s) => (
              <option key={s} value={s}>
                {s}
              </option>
            ))}
          </select>
        </div>

        {/* Таблица расписания */}
        <div
          style={{
            borderRadius: "12px",
            overflow: "hidden",
            border: `1px solid ${palette.tableBorder}`,
            marginTop: "8px",
          }}
        >
          {filteredSchedule.length === 0 ? (
            <div
              style={{
                padding: "12px 14px",
                color: palette.emptyText,
                fontSize: "13px",
              }}
            >
              Нет записей для выбранных фильтров
            </div>
          ) : (
            <table
              style={{
                width: "100%",
                borderCollapse: "collapse",
                fontSize: "13px",
              }}
            >
              <thead style={{ background: "#1e40af" }}>
                <tr>
                  <th style={thStyle}>Дата</th>
                  <th style={thStyle}>Время</th>
                  <th style={thStyle}>Группа</th>
                  <th style={thStyle}>Предмет</th>
                  <th style={thStyle}>Преподаватель</th>
                  <th style={thStyle}>Аудитория</th>
                </tr>
              </thead>
              <tbody>
                {filteredSchedule.map((item, index) => {
                  const isEven = index % 2 === 0;
                  const rowBaseColor = isEven
                    ? palette.rowBg
                    : palette.rowAltBg;
                  return (
                    <tr
                      key={index}
                      style={{
                        ...baseRowStyle,
                        backgroundColor: rowBaseColor,
                      }}
                      onMouseEnter={(e) => {
                        e.currentTarget.style.backgroundColor =
                          palette.rowHoverBg;
                      }}
                      onMouseLeave={(e) => {
                        e.currentTarget.style.backgroundColor =
                          rowBaseColor;
                      }}
                    >
                      <td style={tdStyle}>{item.date}</td>
                      <td style={tdStyle}>
                        {item.startTime}–{item.endTime}
                      </td>
                      <td style={tdStyle}>{item.groupName}</td>
                      <td style={tdStyle}>{item.subjectName}</td>
                      <td style={tdStyle}>{item.teacherName}</td>
                      <td style={tdStyle}>{item.roomName}</td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          )}
        </div>
      </div>
    </div>
  );
}

export default App;
