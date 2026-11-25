#pragma once

namespace rocks {

// Ставит курсор в точку экрана (физические пиксели) и кликает левой кнопкой.
// Блокирует поток на время клика (~50 мс). При ошибке бросает std::runtime_error.
void click_at(int x, int y);

} // namespace rocks
