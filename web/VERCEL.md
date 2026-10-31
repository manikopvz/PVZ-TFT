# Chuyển PVZ-TFT từ GitHub Pages sang Vercel

Game được build và kiểm thử bằng Emscripten trên GitHub Actions. Vercel chỉ nhận kết quả tĩnh đã build, không phải tự biên dịch repo C++.

## 1. Giữ lại tiến trình trên Pages

Merge PR, chờ workflow **WebAssembly and GitHub Pages** trên `main` chạy thành công. Mở lại [Pages hiện tại](https://manikopvz.github.io/PVZ-TFT/), tải lại trang để có thanh icon mới. Bấm icon **mũi tên xuống** để tải `pvz-tft-save.json`. Khi đang chơi, nút này ghi checkpoint trước khi tải.

Save trong IndexedDB thuộc từng origin. Pages, tên miền Vercel và các URL preview không dùng chung save. Không xóa dữ liệu Pages trước khi chuyển xong.

## 2. Tải bản Vercel đã build

Vào repo → **Actions → WebAssembly and GitHub Pages → lần chạy thành công của main → Artifacts → pvz-tft-vercel**. Giải nén vào một thư mục trên máy. Bên trong phải có `.vercel/output/config.json` và `.vercel/output/static/index.html`, `index.js`, `index.wasm`, `index.data`. Bật hiển thị file ẩn nếu chưa thấy `.vercel`.

Có thể tải artifact từ PR để thử trước, nhưng chuyển tiến trình chính sau khi đã cập nhật Pages ở bước 1. Artifact được giữ 14 ngày; chạy lại workflow nếu đã hết hạn.

## 3. Tạo và triển khai project Vercel

Cài Node.js 22 hoặc bản LTS tương thích mới hơn. Mở terminal ngay tại thư mục đã giải nén, nơi có thư mục `.vercel`:

```sh
npx --yes vercel@58.4.4 login
npx --yes vercel@58.4.4 link
npx --yes vercel@58.4.4 deploy --prebuilt --prod
```

Khi `link` hỏi: chọn tài khoản/team của bạn, tạo project mới (ví dụ `pvz-tft`), dùng thư mục hiện tại `./`. Nếu hỏi framework, chọn **Other**. Không cần import repo C++ bằng luồng build tự động từ Git của Vercel. Lệnh deploy dùng trực tiếp Build Output API đã đóng gói; không chạy `vercel build`.

CLI trả về URL production `https://<project>.vercel.app`. Mở URL đó, trước khi bấm Play hãy bấm icon **mũi tên lên**, chọn file JSON từ bước 1, chờ thông báo nhập thành công rồi chơi. Icon nhập bị khóa khi game đã bắt đầu; muốn nhập lại hãy tải lại trang.

Kiểm tra nhạc, hình ảnh, thao tác và tiến trình trên URL production ổn định. Trên mobile: giữ 350 ms để right-click, hai lần giữ liên tiếp để bán Bench, vuốt hai ngón để cuộn. Thanh icon nằm dưới game để không che Menu/SpeedUp.

## 4. Tự động cập nhật Vercel từ main (tùy chọn)

Sau `link`, `.vercel/project.json` trên máy chứa `orgId` và `projectId`. Trong GitHub repo → **Settings → Secrets and variables → Actions**:

| Loại | Tên | Giá trị |
| --- | --- | --- |
| Repository secret | `VERCEL_TOKEN` | Token tạo trong Vercel Account Settings → Tokens, có quyền với team/project đã chọn |
| Repository variable | `VERCEL_ORG_ID` | `orgId` trong `.vercel/project.json` |
| Repository variable | `VERCEL_PROJECT_ID` | `projectId` trong `.vercel/project.json` |
| Repository variable | `DEPLOY_VERCEL` | `true` |

Không commit token hoặc gửi token vào chat. Vào Actions, chạy workflow trên `main` bằng **Run workflow**. Sau khi build và mọi kiểm thử thành công, job `deploy-vercel` sẽ deploy bản production. Các lần push/merge vào `main` sau đó cũng vậy; PR chỉ build/test, không deploy production. Muốn dừng tự deploy Vercel thì đặt `DEPLOY_VERCEL=false`.

Pages tiếp tục hoạt động trong thời gian chuyển đổi. Chỉ tắt Pages trong Settings sau khi đã chuyển save và xác nhận Vercel hoạt động như mong muốn.

## 5. Tên miền

Dùng URL production `*.vercel.app` của project hoặc thêm tên miền bạn sở hữu tại Vercel Project Settings → Domains rồi làm theo bản ghi DNS Vercel cung cấp. Không thể trỏ hostname `manikopvz.github.io` sang Vercel vì đó là tên miền do GitHub quản lý. Đổi domain sau này cũng cần xuất/nhập save. Không có domain cụ thể nên repo không tự thêm redirect.

## Tài liệu chính thức

- [Build Output API](https://vercel.com/docs/build-output-api)
- [Cấu hình static và MIME](https://vercel.com/docs/build-output-api/configuration)
- [CLI deploy --prebuilt](https://vercel.com/docs/cli/deploy)
