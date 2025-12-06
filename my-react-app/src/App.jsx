// src/App.jsx
import { useState, useMemo } from "react";
import { mockResponseGraph, mockResponseSimple } from "./mockData";

function App() {
  const [algorithm, setAlgorithm] = useState("graph"); // graph | simple
  const [data, setData] = useState(mockResponseGraph);
  const [selectedGroup, setSelectedGroup] = useState("all");
  const [theme, setTheme] = useState("dark"); // "dark" | "light"

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

  // имитация запроса к серверу
  const handleGenerate = async () => {
    try {
      const resp = await fetch(
        `https://localhost:8443/api/schedule?algo=${algorithm}`
      );      
      if (!resp.ok) {
        throw new Error(`HTTP ${resp.status}`);
      }
      const json = await resp.json();
      setData(json);
    } catch (e) {
      console.error(e);
      // можно показать ошибку валидации
      setData((prev) => ({
        ...prev,
        validation: {
          ok: false,
          errors: [
            "Не удалось получить данные от сервера: " + String(e.message || e),
          ],
        },
      }));
    }
  };
  
  // список групп для фильтра
  const groupOptions = useMemo(() => {
    const set = new Set();
    data.schedule.forEach((item) => set.add(item.groupName));
    return Array.from(set);
  }, [data]);

  // фильтруем расписание по выбранной группе
  const filteredSchedule = useMemo(() => {
    if (selectedGroup === "all") return data.schedule;
    return data.schedule.filter((item) => item.groupName === selectedGroup);
  }, [data, selectedGroup]);

  // стили для заголовков таблицы и ячеек (зависят от темы)
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
    color: palette.textMain, // ← вот тут цвет текста
    fontSize: "13px",
    whiteSpace: "nowrap",
  };

  const baseRowStyle = {
    transition: "background-color 0.15s ease",
  };

  return (
    <div
      style={{
        minHeight: "100vh",
        margin: 0,
        padding: "20px",
        fontFamily: "system-ui, -apple-system, BlinkMacSystemFont, sans-serif",
        background: palette.pageBg,
        color: palette.textMain,
      }}
    >
      <div
        style={{
          maxWidth: "1000px",
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
              Сравнение алгоритмов <code>graph</code> и <code>simple</code>
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
            {/* переключатель темы */}
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

            <select
              value={algorithm}
              onChange={(e) => setAlgorithm(e.target.value)}
              style={{
                padding: "6px 10px",
                borderRadius: "9999px",
                border: "1px solid #4b5563",
                background:
                  theme === "dark" ? "#020617" : "#ffffff",
                color: palette.textMain,
                fontSize: "13px",
              }}
            >
              <option value="graph">
                Алгоритм: Graph
              </option>
              <option value="simple">Алгоритм: Simple</option>
            </select>

            <button
              onClick={handleGenerate}
              style={{
                padding: "8px 14px",
                borderRadius: "9999px",
                border: "none",
                background:
                  "linear-gradient(135deg, #22c55e 0%, #16a34a 50%, #22c55e 100%)",
                color: "#02120a",
                fontWeight: 600,
                fontSize: "13px",
                cursor: "pointer",
              }}
            >
              Сгенерировать
            </button>
          </div>
        </header>

        {/* Блок статуса и ошибок валидатора */}
        <section
          style={{
            marginBottom: "16px",
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
          <div style={{ marginTop: "6px", color: palette.textMuted }}>
            Используемый алгоритм:{" "}
            <span
              style={{
                color: palette.textMain,
                fontWeight: 500,
              }}
            >
              {data.algorithm}
            </span>
          </div>
        </section>

        {/* Фильтр по группе */}
        <section
          style={{
            marginBottom: "12px",
            display: "flex",
            alignItems: "center",
            gap: "12px",
            fontSize: "13px",
            flexWrap: "wrap",
          }}
        >
          <span style={{ color: palette.textMuted }}>
            Фильтр по группе:
          </span>
          <select
            value={selectedGroup}
            onChange={(e) => setSelectedGroup(e.target.value)}
            style={{
              padding: "4px 10px",
              borderRadius: "9999px",
              border: "1px solid #4b5563",
              background:
                theme === "dark" ? "#020617" : "#ffffff",
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
        </section>

        {/* Таблица расписания */}
        <section
          style={{
            borderRadius: "12px",
            border: `1px solid ${palette.cardBorder}`,
            overflowX: "auto", // для мобильных
          }}
        >
          <table
            style={{
              width: "100%",
              borderCollapse: "collapse",
              fontSize: "13px",
              minWidth: "640px",
            }}
          >
            <thead
              style={{
                background: palette.headerGradient,
              }}
            >
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
              {filteredSchedule.length === 0 ? (
                <tr>
                  <td
                    colSpan={6}
                    style={{
                      padding: "16px",
                      textAlign: "center",
                      color: palette.emptyText,
                    }}
                  >
                    Нет записей для выбранного фильтра
                  </td>
                </tr>
              ) : (
                filteredSchedule.map((item, index) => {
                  const isEven = index % 2 === 0;
                  const rowBaseColor = isEven
                    ? palette.rowBg
                    : palette.rowAltBg;

                  return (
                    <tr
                      key={item.examId}
                      style={{
                        ...baseRowStyle,
                        backgroundColor: rowBaseColor,
                      }}
                      onMouseEnter={(e) =>
                        (e.currentTarget.style.backgroundColor =
                          palette.rowHoverBg)
                      }
                      onMouseLeave={(e) =>
                        (e.currentTarget.style.backgroundColor =
                          rowBaseColor)
                      }
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
                })
              )}
            </tbody>
          </table>
        </section>
      </div>
    </div>
  );
}

export default App;
