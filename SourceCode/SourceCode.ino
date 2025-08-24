#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <FreeRTOSConfig.h>

// =======================================================================================================

// Các chân sử dụng cho các linh kiện
#define DHT_pin  33 // Chân kết nối cảm biến DHT
#define cambienas 26 // Chân kết nối cảm biến ánh sáng
#define cambienmua 32 // Chân kết nối cảm biến mưa
#define buzzer_pin  23 // Chân điều khiển buzzer
#define btnduara    19 // Nút nhấn để đưa giàn phơi ra
#define btnduavao   18 // Nút nhấn để thu giàn phơi vào
#define in1  13 // Điều khiển động cơ quay
#define in2  14 // Điều khiển động cơ quay 
#define rst  16 // Nút reset
#define cthtra  25 // Công tắc hành trình trạng thái ra
#define cthtvao  27 // Công tắc hành trình trạng thái vào

// Khai báo đối tượng LCD và cảm biến DHT
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHT_pin, DHT22);

// Khai báo các biến toàn cục
TimerHandle_t auto_reload_timer; // Bộ đếm thời gian cập nhật giá trị cảm biến
TimerHandle_t autotimerphoido; // Bộ đếm thời gian phơi đồ
SemaphoreHandle_t xMutex; // Đối tượng mutex để tránh xung đột biến dk
SemaphoreHandle_t xBaodongSemaphore; // Semaphore để quản lý báo động
float humidity = 0; // Độ ẩm đọc từ cảm biến DHT
int as = 0; // Giá trị ánh sáng đọc từ cảm biến ánh sáng
int mua = 0; // Trạng thái cảm biến mưa
int ctht = 0; // Trạng thái công tắc hành trình (1: ra ngoài, 0: vào trong)
int dk = 0; // biến điều khiển
int i = 0; // Bộ đếm thời gian phơi đồ

// Khai báo prototype các hàm
void doccambien(TimerHandle_t xTimer);
void SoftwareTimercambien();
void demgio(TimerHandle_t xTimer);
void SoftwareTimertinhthoigianphoi();
void duaradk(void* paramaters);
void autodk(void* paramaters);
void btn(void* paramaters);
void baodong(void* paramaters);
void automode(void* paramaters);
void congtacht(void* paramaters);

void setup() {
    Serial.begin(115200); // Khởi tạo Serial
    
    dht.begin(); // Khởi tạo cảm biến DHT

    // Cấu hình các chân
    pinMode(cambienas, INPUT);
    pinMode(cambienmua, INPUT);
    pinMode(DHT_pin, INPUT);
    pinMode(buzzer_pin, OUTPUT);
    pinMode(btnduara, INPUT_PULLUP);
    pinMode(btnduavao, INPUT_PULLUP);
    pinMode(rst, INPUT_PULLUP);
    pinMode(in1, OUTPUT);
    pinMode(in2, OUTPUT);
    pinMode(cthtra, INPUT_PULLUP);
    pinMode(cthtvao, INPUT_PULLUP);

    // Đưa motor về trạng thái dừng
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);

    // Tạo Mutex BaodongSemaphore
    xBaodongSemaphore = xSemaphoreCreateBinary();
    xMutex = xSemaphoreCreateMutex();

    // Tạo Software Timer
    SoftwareTimercambien();
    SoftwareTimertinhthoigianphoi();

    // Tạo các task FreeRTOS
    xTaskCreatePinnedToCore(duaradk, "Phân tích và đưa ra điều khiển", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(autodk, "Tự động điều khiển", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(btn, "Nút nhấn thủ công", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(baodong, "Báo động", 4096, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(automode, "Auto mode và reset", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(congtacht, "công tắc ht", 4096, NULL, 3, NULL, 1);

    // Khởi tạo LCD
    lcd.init();
    lcd.backlight();
}

void loop() {
    // Không có gì trong loop vì FreeRTOS quản lý các task
}

// =======================================================================================================

// Tạo timer để đọc cảm biến định kỳ
void SoftwareTimercambien() {
    auto_reload_timer = xTimerCreate(
        "Auto-reload timer",
        pdMS_TO_TICKS(1000), // Chu kỳ 1 giây
        pdTRUE,              // Tự động lặp lại
        (void*)0,
        doccambien           // Callback đọc cảm biến
    );
    xTimerStart(auto_reload_timer, 0);
}

// Tạo timer để tính thời gian phơi đồ
void SoftwareTimertinhthoigianphoi() {
    autotimerphoido = xTimerCreate(
        "Đếm thời gian phơi",
        pdMS_TO_TICKS(60000), // Chu kỳ 1 phút
        pdTRUE,
        (void*)0,
        demgio // Callback demgio
    );
    xTimerStart(autotimerphoido, 0);
}

// Hàm đọc cảm biến và cập nhật LCD
void doccambien(TimerHandle_t xTimer) {
    humidity = dht.readHumidity(); // Đọc độ ẩm từ cảm biến DHT
    as = analogRead(cambienas); // Đọc giá trị cảm biến ánh sáng
    mua = digitalRead(cambienmua); // Đọc trạng thái cảm biến mưa
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Do am: ");
    lcd.print(humidity);
    lcd.print("%");

    lcd.setCursor(0, 1);
    if (ctht == 1) {
        lcd.print("Da phoi: ");
        lcd.print(i);
        lcd.print("p"); // Hiển thị thời gian đã phơi
    } else if (ctht == 0) {
        lcd.print("Da thu vao"); // Hiển thị trạng thái đã thu vào
    } 
}

// Hàm đếm thời gian phơi đồ
void demgio(TimerHandle_t xTimer) {
    if (ctht == 1 && i != 2) {
        i++; // Tăng thời gian phơi mỗi phút
    }
    if (i == 2) {
        dk = 0; // Thu đồ vào khi đủ thời gian (2 phút cho kiểm tra chức năng)
    }
}

// Đưa ra điều khiển dựa trên ánh sáng và mưa
void duaradk(void* paramaters) {
    while (1) {  
        while (i == 2) {
            vTaskDelay(pdMS_TO_TICKS(1)); // Không đưa ra điều khiển nếu đủ thời gian phơi
        }
        xSemaphoreTake(xMutex, portMAX_DELAY);    
        if (mua == 1 && as > 2000) {
            dk = 0; // Không mưa và trời tối
        } else if (mua == 1 && as < 2000) {
            dk = 1; // Không mưa và trời sáng
        } else if (mua == 0 && as > 2000) {
            dk = 0; // Mưa và trời tối
        } else if (mua == 0 && as < 2000) {
            dk = 0; // Mưa và trời sáng
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        xSemaphoreGive(xMutex);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// Tự động điều khiển giàn phơi
void autodk(void* paramaters) {
    while (1) {
        if (dk == 1 && ctht != 1) { // Điều kiện đưa ra
            digitalWrite(in1, HIGH);
            digitalWrite(in2, LOW);
            xSemaphoreGive(xBaodongSemaphore); // Cấp quyền semaphore báo động
            while (ctht != 1) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        } else if (dk == 0 && ctht != 0) { // Điều kiện thu vào
            digitalWrite(in1, LOW); 
            digitalWrite(in2, HIGH);
            xSemaphoreGive(xBaodongSemaphore); // Cấp quyền semaphore báo động
            while (ctht != 0) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
        digitalWrite(in1, LOW); // Dừng động cơ
        digitalWrite(in2, LOW);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Điều khiển thủ công bằng nút nhấn
void btn(void* paramaters) {
    while (1) {
        if (digitalRead(btnduara) == LOW) {
            xSemaphoreTake(xMutex, portMAX_DELAY); // Sử dụng mutex tránh xung đột
            i = 0; // Reset thời gian phơi
            dk = 1; // Đưa ra
            while (mua == 1 && digitalRead(btnduavao) == HIGH && digitalRead(rst) == HIGH && i != 2) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            xSemaphoreGive(xMutex);
        } else if (digitalRead(btnduavao) == LOW) {
            xSemaphoreTake(xMutex, portMAX_DELAY);
            dk = 0; // Thu vào
            while (digitalRead(btnduara) == HIGH && digitalRead(rst) == HIGH) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            xSemaphoreGive(xMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Task xử lý báo động
void baodong(void* paramaters) {
    while (1) {
        xSemaphoreTake(xBaodongSemaphore, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (dk == 1 && ctht == 0) {
            digitalWrite(buzzer_pin, HIGH); // Bật buzzer
            while (ctht == 0) {
                vTaskDelay(pdMS_TO_TICKS(50)); // Cảnh báo cho tới khi hoàn thành đưa ra
            }
        } 
        digitalWrite(buzzer_pin, LOW);
        xSemaphoreTake(xBaodongSemaphore, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (dk == 0 && ctht == 1) {
            digitalWrite(buzzer_pin, HIGH);
            while (ctht == 1) {
                vTaskDelay(pdMS_TO_TICKS(50)); // Cảnh báo cho tới khi hoàn thành thu vào
            }
        }
        digitalWrite(buzzer_pin, LOW);
    }
}

// Task reset và chuyển về chế độ tự động
void automode(void* paramaters) { 
    while (1) {
        if (digitalRead(rst) == LOW) { // Xử lý khi nhấn nút reset
            i = 0; // Reset thời gian phơi
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// Task xử lý công tắc hành trình
void congtacht(void* paramaters) {
    while (1) {
        if (digitalRead(cthtra) == LOW) {
            ctht = 1; // Trạng thái đã ra ngoài
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        if (digitalRead(cthtvao) == LOW) {
            ctht = 0; // Trạng thái đã vào trong
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
