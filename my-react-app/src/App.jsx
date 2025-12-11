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

  // исходные данные (config)
  const [config, setConfig] = useState(createEmptyConfig());

  // какую сущность редактируем сейчас (таб)
  const [configTab, setConfigTab] = useState("groups"); // groups | teachers | subjects | rooms | exams | session

  // --- авторизация ---
  const [user, setUser] = useState(null); // { id, username, role } или null
  const [authLoading, setAuthLoading] = useState(false);
  const [authError, setAuthError] = useState("");
  const [loginForm, setLoginForm] = useState({
    username: "",
    password: "",
  });

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

  // при загрузке страницы пробуем узнать текущего пользователя по куке
  useEffect(() => {
    fetch("/api/auth/me")
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
        // тихо игнорируем, просто считаем, что не залогинен
      });
  }, []);

  const handleLoginInputChange = (e) => {
    const { name, value } = e.target;
    setLoginForm((prev) => ({ ...prev, [name]: value }));
  };

  const handleLoginSubmit = async (e) => {
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

      const data = await res.json();
      if (data.ok && data.user) {
        setUser(data.user);
        setLoginForm({ username: "", password: "" });
        setAuthError("");
      } else {
        setAuthError("Некорректный ответ сервера");
      }
    } catch (err) {
      setAuthError("Сетевая ошибка");
    } finally {
      setAuthLoading(false);
    }
  };

  const handleLogout = () => {
    // пока просто чистим фронтовое состояние
    setUser(null);
    // при желании потом сделаем /api/auth/logout с очисткой куки
  };

  // запрос к C++ серверу
  const handleGenerate = async () => {
    // защита: без входа не дергаем API
    if (!user) {
      setErrorMsg("Сначала войдите в систему, чтобы генерировать расписание");
      return;
    }

    setLoading(true);
    setErrorMsg(null);

    try {
      const payload = {
        algo: "graph", // всегда графовый алгоритм
        config, // весь config из редактора
      };

      const resp = await fetch("/api/schedule", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
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

  // обновление поля сущности по id
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
        subjects: [], // ID предметов
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

  // --- работа с предметами преподавателя ---

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

  // --- JSX-редакторы для config ---

  const renderGroupsEditor = () => (
    <div style={{ marginTop: "8px" }}>
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          marginBottom: "6px",
          alignItems: "center",
        }}
      >
        <span style={{ fontWeight: 500 }}>Группы</span>
        <button
          onClick={addGroup}
          style={{
            padding: "4px 10px",
            borderRadius: "9999px",
            border: `1px solid ${palette.cardBorder}`,
            background: theme === "dark" ? "#020617" : "#ffffff",
            color: palette.textMain,
            fontSize: "12px",
            cursor: "pointer",
          }}
        >
          Добавить группу
        </button>
      </div>

      {(!config.groups || config.groups.length === 0) && (
        <div
          style={{
            fontSize: "12px",
            color: palette.textMuted,
            marginBottom: "4px",
          }}
        >
          Пока нет ни одной группы. Добавь первую 🙂
        </div>
      )}

      {config.groups?.map((g) => (
        <div
          key={g.id}
          style={{
            display: "grid",
            gridTemplateColumns: "60px 1fr 180px 90px",
            gap: "6px",
            alignItems: "center",
            marginBottom: "4px",
            fontSize: "12px",
          }}
        >
          <span style={{ color: palette.textMuted }}>id: {g.id}</span>
          <input
            type="text"
            value={g.name}
            onChange={(e) =>
              updateItemField("groups", g.id, "name", e.target.value)
            }
            placeholder="Название группы"
            style={{
              padding: "4px 6px",
              borderRadius: "6px",
              border: `1px solid ${palette.cardBorder}`,
              background:
                theme === "dark" ? "rgba(15,23,42,0.8)" : "#ffffff",
              color: palette.textMain,
            }}
          />
          <div
            style={{
              display: "flex",
              flexDirection: "column",
              alignItems: "flex-start",
              gap: "2px",
            }}
          >
            <input
              type="number"
              value={g.size ?? 0}
              onChange={(e) =>
                updateItemField(
                  "groups",
                  g.id,
                  "size",
                  parseInt(e.target.value || "0", 10)
                )
              }
              min={0}
              style={{
                width: "100%",
                padding: "4px 6px",
                borderRadius: "6px",
                border: `1px solid ${palette.cardBorder}`,
                background:
                  theme === "dark"
                    ? "rgba(15,23,42,0.8)"
                    : "#ffffff",
                color: palette.textMain,
              }}
            />
            <span
              style={{
                fontSize: "11px",
                color: palette.textMuted,
              }}
            >
              студентов
            </span>
          </div>
          <button
            onClick={() => deleteItem("groups", g.id)}
            style={{
              padding: "4px 6px",
              borderRadius: "9999px",
              border: "none",
              background: "rgba(220,38,38,0.12)",
              color: "#ef4444",
              fontSize: "11px",
              cursor: "pointer",
            }}
          >
            Удалить
          </button>
        </div>
      ))}
    </div>
  );

  const renderTeachersEditor = () => (
    <div style={{ marginTop: "8px" }}>
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          marginBottom: "6px",
          alignItems: "center",
        }}
      >
        <span style={{ fontWeight: 500 }}>Преподаватели</span>
        <button
          onClick={addTeacher}
          style={{
            padding: "4px 10px",
            borderRadius: "9999px",
            border: `1px solid ${palette.cardBorder}`,
            background: theme === "dark" ? "#020617" : "#ffffff",
            color: palette.textMain,
            fontSize: "12px",
            cursor: "pointer",
          }}
        >
          Добавить преподавателя
        </button>
      </div>

      {(!config.teachers || config.teachers.length === 0) && (
        <div
          style={{
            fontSize: "12px",
            color: palette.textMuted,
            marginBottom: "4px",
          }}
        >
          Пока ни одного преподавателя.
        </div>
      )}

      {config.teachers?.map((t) => {
        const teacherSubjects = t.subjects || [];
        return (
          <div
            key={t.id}
            style={{
              marginBottom: "8px",
              paddingBottom: "6px",
              borderBottom: `1px dashed ${palette.cardBorder}`,
              fontSize: "12px",
            }}
          >
            <div
              style={{
                display: "grid",
                gridTemplateColumns: "60px 1fr 80px",
                gap: "6px",
                alignItems: "center",
                marginBottom: "4px",
              }}
            >
              <span style={{ color: palette.textMuted }}>
                id: {t.id}
              </span>
              <input
                type="text"
                value={t.name}
                onChange={(e) =>
                  updateItemField(
                    "teachers",
                    t.id,
                    "name",
                    e.target.value
                  )
                }
                placeholder="ФИО преподавателя"
                style={{
                  padding: "4px 6px",
                  borderRadius: "6px",
                  border: `1px solid ${palette.cardBorder}`,
                  background:
                    theme === "dark"
                      ? "rgba(15,23,42,0.8)"
                      : "#ffffff",
                  color: palette.textMain,
                }}
              />
              <button
                onClick={() => deleteItem("teachers", t.id)}
                style={{
                  padding: "4px 6px",
                  borderRadius: "9999px",
                  border: "none",
                  background: "rgba(220,38,38,0.12)",
                  color: "#ef4444",
                  fontSize: "11px",
                  cursor: "pointer",
                }}
              >
                Удалить
              </button>
            </div>

            <div
              style={{
                marginLeft: "60px",
                display: "flex",
                flexWrap: "wrap",
                gap: "6px",
                alignItems: "center",
              }}
            >
              <span
                style={{
                  color: palette.textMuted,
                  fontSize: "11px",
                }}
              >
                Ведёт предметы:
              </span>

              <select
                value=""
                onChange={(e) => {
                  const val = e.target.value;
                  if (!val) return;
                  const sid = parseInt(val, 10);
                  addTeacherSubject(t.id, sid);
                }}
                style={{
                  padding: "4px 6px",
                  borderRadius: "9999px",
                  border: `1px solid ${palette.cardBorder}`,
                  background:
                    theme === "dark"
                      ? "rgba(15,23,42,0.8)"
                      : "#ffffff",
                  color: palette.textMain,
                  fontSize: "11px",
                }}
              >
                <option value="">+ предмет…</option>
                {config.subjects?.map((s) => (
                  <option
                    key={s.id}
                    value={s.id}
                    disabled={teacherSubjects.includes(s.id)}
                  >
                    {s.name || `Предмет ${s.id}`}
                  </option>
                ))}
              </select>

              {teacherSubjects.length === 0 ? (
                <span
                  style={{
                    fontSize: "11px",
                    color: palette.textMuted,
                  }}
                >
                  пока не назначены
                </span>
              ) : (
                <div
                  style={{
                    display: "flex",
                    flexWrap: "wrap",
                    gap: "4px",
                  }}
                >
                  {teacherSubjects.map((sid) => {
                    const subj =
                      config.subjects?.find((s) => s.id === sid) ||
                      null;
                    const label =
                      subj?.name || `Предмет ${sid}`;
                    return (
                      <span
                        key={sid}
                        style={{
                          display: "inline-flex",
                          alignItems: "center",
                          gap: "4px",
                          padding: "2px 6px",
                          borderRadius: "9999px",
                          border: `1px solid ${palette.cardBorder}`,
                          background:
                            theme === "dark"
                              ? "rgba(15,23,42,0.8)"
                              : "#ffffff",
                        }}
                      >
                        {label}
                        <button
                          onClick={() =>
                            removeTeacherSubject(t.id, sid)
                          }
                          style={{
                            border: "none",
                            background: "transparent",
                            color: palette.textMuted,
                            fontSize: "10px",
                            cursor: "pointer",
                          }}
                          title="Убрать этот предмет у преподавателя"
                        >
                          ×
                        </button>
                      </span>
                    );
                  })}
                </div>
              )}
            </div>
          </div>
        );
      })}
    </div>
  );

  const renderSubjectsEditor = () => (
    <div style={{ marginTop: "8px" }}>
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          marginBottom: "6px",
          alignItems: "center",
        }}
      >
        <span style={{ fontWeight: 500 }}>Предметы</span>
        <button
          onClick={addSubject}
          style={{
            padding: "4px 10px",
            borderRadius: "9999px",
            border: `1px solid ${palette.cardBorder}`,
            background: theme === "dark" ? "#020617" : "#ffffff",
            color: palette.textMain,
            fontSize: "12px",
            cursor: "pointer",
          }}
        >
          Добавить предмет
        </button>
      </div>

      {(!config.subjects || config.subjects.length === 0) && (
        <div
          style={{
            fontSize: "12px",
            color: palette.textMuted,
            marginBottom: "4px",
          }}
        >
          Пока нет предметов.
        </div>
      )}

      {config.subjects?.map((s) => (
        <div
          key={s.id}
          style={{
            display: "grid",
            gridTemplateColumns: "60px 1fr 170px 80px",
            gap: "6px",
            alignItems: "center",
            marginBottom: "4px",
            fontSize: "12px",
          }}
        >
          <span style={{ color: palette.textMuted }}>id: {s.id}</span>
          <input
            type="text"
            value={s.name}
            onChange={(e) =>
              updateItemField("subjects", s.id, "name", e.target.value)
            }
            placeholder="Название предмета"
            style={{
              padding: "4px 6px",
              borderRadius: "6px",
              border: `1px solid ${palette.cardBorder}`,
              background:
                theme === "dark" ? "rgba(15,23,42,0.8)" : "#ffffff",
              color: palette.textMain,
            }}
          />
          <div
            style={{
              display: "flex",
              alignItems: "center",
              gap: "4px",
            }}
          >
            <input
              type="number"
              min={1}
              max={5}
              value={s.difficulty ?? 3}
              onChange={(e) =>
                updateItemField(
                  "subjects",
                  s.id,
                  "difficulty",
                  Math.min(
                    5,
                    Math.max(1, parseInt(e.target.value || "3", 10))
                  )
                )
              }
              style={{
                padding: "4px 6px",
                borderRadius: "6px",
                border: `1px solid ${palette.cardBorder}`,
                background:
                  theme === "dark"
                    ? "rgba(15,23,42,0.8)"
                    : "#ffffff",
                color: palette.textMain,
              }}
              title="Сложность экзамена: 1 — очень легко, 5 — очень сложно"
            />
            <span
              style={{
                fontSize: "11px",
                color: palette.textMuted,
              }}
            >
              сложность 1–5
            </span>
          </div>
          <button
            onClick={() => deleteItem("subjects", s.id)}
            style={{
              padding: "4px 6px",
              borderRadius: "9999px",
              border: "none",
              background: "rgba(220,38,38,0.12)",
              color: "#ef4444",
              fontSize: "11px",
              cursor: "pointer",
            }}
          >
            Удалить
          </button>
        </div>
      ))}
    </div>
  );

  const renderRoomsEditor = () => (
    <div style={{ marginTop: "8px" }}>
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          marginBottom: "6px",
          alignItems: "center",
        }}
      >
        <span style={{ fontWeight: 500 }}>Аудитории</span>
        <button
          onClick={addRoom}
          style={{
            padding: "4px 10px",
            borderRadius: "9999px",
            border: `1px solid ${palette.cardBorder}`,
            background: theme === "dark" ? "#020617" : "#ffffff",
            color: palette.textMain,
            fontSize: "12px",
            cursor: "pointer",
          }}
        >
          Добавить аудиторию
        </button>
      </div>

      {(!config.rooms || config.rooms.length === 0) && (
        <div
          style={{
            fontSize: "12px",
            color: palette.textMuted,
            marginBottom: "4px",
          }}
        >
          Пока нет аудиторий.
        </div>
      )}

      {config.rooms?.map((r) => (
        <div
          key={r.id}
          style={{
            display: "grid",
            gridTemplateColumns: "60px 1fr 160px 80px",
            gap: "6px",
            alignItems: "center",
            marginBottom: "4px",
            fontSize: "12px",
          }}
        >
          <span style={{ color: palette.textMuted }}>id: {r.id}</span>
          <input
            type="text"
            value={r.name}
            onChange={(e) =>
              updateItemField("rooms", r.id, "name", e.target.value)
            }
            placeholder="Название аудитории"
            style={{
              padding: "4px 6px",
              borderRadius: "6px",
              border: `1px solid ${palette.cardBorder}`,
              background:
                theme === "dark" ? "rgba(15,23,42,0.8)" : "#ffffff",
              color: palette.textMain,
            }}
          />
          <div
            style={{
              display: "flex",
              alignItems: "center",
              gap: "4px",
            }}
          >
            <input
              type="number"
              min={1}
              value={r.capacity ?? 30}
              onChange={(e) =>
                updateItemField(
                  "rooms",
                  r.id,
                  "capacity",
                  parseInt(e.target.value || "30", 10)
                )
              }
              style={{
                padding: "4px 6px",
                borderRadius: "6px",
                border: `1px solid ${palette.cardBorder}`,
                background:
                  theme === "dark"
                    ? "rgba(15,23,42,0.8)"
                    : "#ffffff",
                color: palette.textMain,
              }}
            />
            <span
              style={{
                fontSize: "11px",
                color: palette.textMuted,
              }}
            >
              мест
            </span>
          </div>
          <button
            onClick={() => deleteItem("rooms", r.id)}
            style={{
              padding: "4px 6px",
              borderRadius: "9999px",
              border: "none",
              background: "rgba(220,38,38,0.12)",
              color: "#ef4444",
              fontSize: "11px",
              cursor: "pointer",
            }}
          >
            Удалить
          </button>
        </div>
      ))}
    </div>
  );

  const renderExamsEditor = () => (
    <div style={{ marginTop: "8px" }}>
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          marginBottom: "6px",
          alignItems: "center",
        }}
      >
        <span style={{ fontWeight: 500 }}>Экзамены</span>
        <button
          onClick={addExam}
          style={{
            padding: "4px 10px",
            borderRadius: "9999px",
            border: `1px solid ${palette.cardBorder}`,
            background: theme === "dark" ? "#020617" : "#ffffff",
            color: palette.textMain,
            fontSize: "12px",
            cursor: "pointer",
          }}
        >
          Добавить экзамен
        </button>
      </div>

      {(!config.exams || config.exams.length === 0) && (
        <div
          style={{
            fontSize: "12px",
            color: palette.textMuted,
            marginBottom: "4px",
          }}
        >
          Пока нет экзаменов.
        </div>
      )}

      {config.exams?.map((e) => {
        const subjectId = e.subjectId ?? null;
        const allTeachers = config.teachers || [];

        let filteredTeachers = allTeachers;
        let filteredBySubject = [];
        if (subjectId != null) {
          filteredBySubject = allTeachers.filter((t) =>
            (t.subjects || []).includes(subjectId)
          );
          if (filteredBySubject.length > 0) {
            filteredTeachers = filteredBySubject;
          }
        }

        const subject =
          config.subjects?.find((s) => s.id === subjectId) || null;

        const showFilterHint =
          subjectId != null && filteredBySubject.length > 0;

        return (
          <div
            key={e.id}
            style={{
              display: "grid",
              gridTemplateColumns:
                "60px minmax(120px, 1fr) minmax(140px, 1fr) minmax(140px, 1fr) 180px 90px",
              gap: "6px",
              alignItems: "center",
              marginBottom: "4px",
              fontSize: "12px",
            }}
          >
            <span style={{ color: palette.textMuted }}>id: {e.id}</span>

            {/* группа */}
            <select
              value={e.groupId ?? ""}
              onChange={(ev) =>
                updateItemField(
                  "exams",
                  e.id,
                  "groupId",
                  ev.target.value ? parseInt(ev.target.value, 10) : null
                )
              }
              style={{
                padding: "4px 6px",
                borderRadius: "6px",
                border: `1px solid ${palette.cardBorder}`,
                background:
                  theme === "dark"
                    ? "rgba(15,23,42,0.8)"
                    : "#ffffff",
                color: palette.textMain,
              }}
            >
              <option value="">Группа…</option>
              {config.groups?.map((g) => (
                <option key={g.id} value={g.id}>
                  {g.name || `Группа ${g.id}`}
                </option>
              ))}
            </select>

            {/* преподаватель (фильтр по предмету) */}
            <div
              style={{
                display: "flex",
                flexDirection: "column",
                gap: "2px",
              }}
            >
              <select
                value={e.teacherId ?? ""}
                onChange={(ev) =>
                  updateItemField(
                    "exams",
                    e.id,
                    "teacherId",
                    ev.target.value ? parseInt(ev.target.value, 10) : null
                  )
                }
                style={{
                  padding: "4px 6px",
                  borderRadius: "6px",
                  border: `1px solid ${palette.cardBorder}`,
                  background:
                    theme === "dark"
                      ? "rgba(15,23,42,0.8)"
                      : "#ffffff",
                  color: palette.textMain,
                }}
              >
                <option value="">Преподаватель…</option>
                {filteredTeachers.map((t) => (
                  <option key={t.id} value={t.id}>
                    {t.name || `Преподаватель ${t.id}`}
                  </option>
                ))}
              </select>
              {showFilterHint && (
                <span
                  style={{
                    fontSize: "11px",
                    color: palette.textMuted,
                  }}
                >
                  Показаны только преподы, ведущие «
                  {subject?.name || `Предмет ${subjectId}`}
                  »
                </span>
              )}
            </div>

            {/* предмет */}
            <select
              value={e.subjectId ?? ""}
              onChange={(ev) =>
                updateItemField(
                  "exams",
                  e.id,
                  "subjectId",
                  ev.target.value ? parseInt(ev.target.value, 10) : null
                )
              }
              style={{
                padding: "4px 6px",
                borderRadius: "6px",
                border: `1px solid ${palette.cardBorder}`,
                background:
                  theme === "dark"
                    ? "rgba(15,23,42,0.8)"
                    : "#ffffff",
                color: palette.textMain,
              }}
            >
              <option value="">Предмет…</option>
              {config.subjects?.map((s) => (
                <option key={s.id} value={s.id}>
                  {s.name || `Предмет ${s.id}`}
                </option>
              ))}
            </select>

            {/* длительность */}
            <div
              style={{
                display: "flex",
                flexDirection: "column",
                alignItems: "flex-start",
                gap: "2px",
              }}
            >
              <input
                type="number"
                min={30}
                step={30}
                value={e.durationMinutes ?? 120}
                onChange={(ev) =>
                  updateItemField(
                    "exams",
                    e.id,
                    "durationMinutes",
                    parseInt(ev.target.value || "120", 10)
                  )
                }
                style={{
                  width: "100%",
                  padding: "4px 6px",
                  borderRadius: "6px",
                  border: `1px solid ${palette.cardBorder}`,
                  background:
                    theme === "dark"
                      ? "rgba(15,23,42,0.8)"
                      : "#ffffff",
                  color: palette.textMain,
                }}
                title="Длительность экзамена в минутах"
              />
              <span
                style={{
                  fontSize: "11px",
                  color: palette.textMuted,
                }}
              >
                минут
              </span>
            </div>

            <button
              onClick={() => deleteItem("exams", e.id)}
              style={{
                padding: "4px 6px",
                borderRadius: "9999px",
                border: "none",
                background: "rgba(220,38,38,0.12)",
                color: "#ef4444",
                fontSize: "11px",
                cursor: "pointer",
              }}
            >
              Удалить
            </button>
          </div>
        );
      })}
    </div>
  );

  // --- редактор параметров сессии ---
  const renderSessionEditor = () => (
    <div style={{ marginTop: "8px", fontSize: "12px" }}>
      <div style={{ marginBottom: "10px", fontWeight: 500 }}>
        Параметры сессии
      </div>

      <div
        style={{
          display: "grid",
          gridTemplateColumns: "160px 1fr",
          gap: "8px",
          alignItems: "center",
          marginBottom: "6px",
        }}
      >
        <span style={{ color: palette.textMuted }}>Дата начала</span>
        <input
          type="date"
          value={config.session.start}
          onChange={(e) =>
            setConfig((prev) => ({
              ...prev,
              session: { ...prev.session, start: e.target.value },
            }))
          }
          style={{
            padding: "4px 6px",
            borderRadius: "6px",
            border: `1px solid ${palette.cardBorder}`,
            background: theme === "dark" ? "#020617" : "#ffffff",
            color: palette.textMain,
          }}
        />
      </div>

      <div
        style={{
          display: "grid",
          gridTemplateColumns: "160px 1fr",
          gap: "8px",
          alignItems: "center",
          marginBottom: "6px",
        }}
      >
        <span style={{ color: palette.textMuted }}>Дата окончания</span>
        <input
          type="date"
          value={config.session.end}
          onChange={(e) =>
            setConfig((prev) => ({
              ...prev,
              session: { ...prev.session, end: e.target.value },
            }))
          }
          style={{
            padding: "4px 6px",
            borderRadius: "6px",
            border: `1px solid ${palette.cardBorder}`,
            background: theme === "dark" ? "#020617" : "#ffffff",
            color: palette.textMain,
          }}
        />
      </div>

      <div
        style={{
          display: "grid",
          gridTemplateColumns: "160px 1fr",
          gap: "8px",
          alignItems: "center",
        }}
      >
        <span style={{ color: palette.textMuted }}>
          Максимум экзаменов в день
        </span>
        <input
          type="number"
          min={1}
          max={10}
          value={config.session.maxExamsPerDayForGroup}
          onChange={(e) =>
            setConfig((prev) => ({
              ...prev,
              session: {
                ...prev.session,
                maxExamsPerDayForGroup: parseInt(
                  e.target.value || "1",
                  10
                ),
              },
            }))
          }
          style={{
            width: "80px",
            padding: "4px 6px",
            borderRadius: "6px",
            border: `1px solid ${palette.cardBorder}`,
            background: theme === "dark" ? "#020617" : "#ffffff",
            color: palette.textMain,
          }}
        />
      </div>
    </div>
  );

  const renderConfigEditorTab = () => {
    if (configTab === "groups") return renderGroupsEditor();
    if (configTab === "teachers") return renderTeachersEditor();
    if (configTab === "subjects") return renderSubjectsEditor();
    if (configTab === "rooms") return renderRoomsEditor();
    if (configTab === "exams") return renderExamsEditor();
    if (configTab === "session") return renderSessionEditor();
    return null;
  };

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
                  <b>{user.username || `user#${user.id}`}</b>{" "}
                  ({user.role})
                </div>
                <div
                  style={{
                    fontSize: "12px",
                    color: palette.textMuted,
                    marginTop: "2px",
                  }}
                >
                  Вы можете генерировать расписание и, при роли
                  admin, пользоваться админскими функциями.
                </div>
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
              onSubmit={handleLoginSubmit}
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
                name="username"
                placeholder="Логин"
                value={loginForm.username}
                onChange={handleLoginInputChange}
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
                name="password"
                placeholder="Пароль"
                value={loginForm.password}
                onChange={handleLoginInputChange}
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
                  cursor: "pointer",
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
                  Для генерации расписания требуется вход.
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
              Генерация расписания экзаменов
            </h1>
            <p
              style={{
                margin: "4px 0 0",
                fontSize: "13px",
                color: palette.textMuted,
              }}
            >
              Исходные данные → JSON → генерация расписания
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

            <button
              onClick={handleGenerate}
              disabled={!user || loading}
              title={
                !user
                  ? "Нужно войти, чтобы генерировать расписание"
                  : ""
              }
              style={{
                padding: "8px 14px",
                borderRadius: "9999px",
                border: "none",
                background:
                  "linear-gradient(135deg, #22c55e 0%, #16a34a 50%, #22c55e 100%)",
                color: "#02120a",
                fontWeight: 600,
                fontSize: "13px",
                cursor: !user || loading ? "not-allowed" : "pointer",
                opacity: !user || loading ? 0.7 : 1,
              }}
            >
              Сгенерировать
            </button>
          </div>
        </header>

        {/* БЛОК РАБОТЫ С CONFIG / JSON */}
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

          {/* Табы редактора */}
          <div
            style={{
              display: "flex",
              flexWrap: "wrap",
              gap: "6px",
              marginBottom: "6px",
              fontSize: "12px",
            }}
          >
            {[
              { id: "groups", label: "Группы" },
              { id: "teachers", label: "Преподаватели" },
              { id: "subjects", label: "Предметы" },
              { id: "rooms", label: "Аудитории" },
              { id: "exams", label: "Экзамены" },
              { id: "session", label: "Сессия" },
            ].map((tab) => (
              <button
                key={tab.id}
                onClick={() => setConfigTab(tab.id)}
                style={{
                  padding: "4px 10px",
                  borderRadius: "9999px",
                  border:
                    configTab === tab.id
                      ? "1px solid #3b82f6"
                      : `1px solid ${palette.cardBorder}`,
                  background:
                    configTab === tab.id
                      ? "rgba(59,130,246,0.12)"
                      : theme === "dark"
                      ? "#020617"
                      : "#ffffff",
                  color:
                    configTab === tab.id
                      ? "#60a5fa"
                      : palette.textMain,
                  cursor: "pointer",
                }}
              >
                {tab.label}
              </button>
            ))}
          </div>

          {/* Редактор выбранной сущности */}
          <div
            style={{
              marginTop: "4px",
              paddingTop: "4px",
              borderTop: `1px dashed ${palette.cardBorder}`,
            }}
          >
            {renderConfigEditorTab()}
          </div>
        </section>

        {/* Блок статуса и ошибок валидатора */}
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

          {/* Фильтр по группе */}
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

          {/* Фильтр по преподавателю */}
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

          {/* Фильтр по предмету */}
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

