#pragma once
#include <iostream>
#include <filesystem>
#include <sstream>
#include <vector>
#include <array>
#include <limits>
#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <fstream>
#include <nlohmann/json.hpp>
#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include <cryptopp/hex.h>
#include "helper.h"

constexpr double PI = 3.14159265358979323846;

namespace BTEGeoConventor {
    
    namespace utils {
        class InvertableVectorField {
        private:
            double ROOT3 = sqrt(3);
            std::vector<std::vector<double>> vector_x, vector_y;
            int side_length;
        public:
            InvertableVectorField(std::vector<std::vector<double>> vector_x, std::vector<std::vector<double>> vector_y) {
                side_length = vector_x.size() - 1;
                this->vector_x = vector_x;
                this->vector_y = vector_y;
            }
            std::array<double, 6> get_interpolated_vector(double x, double y) {
                x *= side_length;
                y *= side_length;

                double v = 2 * y / ROOT3;
                double u = x - v * 0.5;

                int u1 = (std::max)(0, (std::min)(int(u), side_length - 1));
                int v1 = (std::max)(0, (std::min)(int(v), side_length - u1 - 1));

                int flip = 1;
                double valx1, valy1, valx2, valy2, valx3, valy3, y3, x3;

                if (y < -ROOT3 * (x - u1 - v1 - 1) || v1 == side_length - u1 - 1) {
                    valx1 = vector_x[u1][v1];
                    valy1 = vector_y[u1][v1];
                    valx2 = vector_x[u1][v1 + 1];
                    valy2 = vector_y[u1][v1 + 1];
                    valx3 = vector_x[u1 + 1][v1];
                    valy3 = vector_y[u1 + 1][v1];

                    y3 = 0.5 * ROOT3 * v1;
                    x3 = (u1 + 1) + 0.5 * v1;
                }
                else {
                    valx1 = vector_x[u1][v1 + 1];
                    valy1 = vector_y[u1][v1 + 1];
                    valx2 = vector_x[u1 + 1][v1];
                    valy2 = vector_y[u1 + 1][v1];
                    valx3 = vector_x[u1 + 1][v1 + 1];
                    valy3 = vector_y[u1 + 1][v1 + 1];

                    flip = -1;
                    y = -y;

                    y3 = -(0.5 * ROOT3 * (v1 + 1));
                    x3 = (u1 + 1) + 0.5 * (v1 + 1);
                }
                double w1 = -(y - y3) / ROOT3 - (x - x3);
                double w2 = 2 * (y - y3) / ROOT3;
                double w3 = 1 - w1 - w2;

                double val_x = valx1 * w1 + valx2 * w2 + valx3 * w3;
                double val_y = valy1 * w1 + valy2 * w2 + valy3 * w3;

                double dfdx = (valx3 - valx1) * side_length;
                double dfdy = side_length * flip * (2 * valx2 - valx1 - valx3) / ROOT3;
                double dgdx = (valy3 - valy1) * side_length;
                double dgdy = side_length * flip * (2 * valy2 - valy1 - valy3) / ROOT3;

                return { val_x, val_y, dfdx, dfdy, dgdx, dgdy };
            }
            std::array<double, 2> apply_newtons_method(double expected_f, double expected_g, double x_est, double y_est, int iterations) {
                for (int i = 0; i < iterations; i++) {
                    std::array<double, 6> val = get_interpolated_vector(x_est, y_est);

                    double f = val[0] - expected_f;
                    double g = val[1] - expected_g;

                    double determinant = 1.0 / (val[2] * val[5] - val[3] * val[4]);

                    x_est -= determinant * (val[5] * f - val[3] * g);
                    y_est -= determinant * (-val[4] * f + val[2] * g);
                }
                return { x_est, y_est };
            }
        };
    }
    namespace data {
        inline std::string base64_decode_cryptopp(const std::string& encoded) {
            std::string decoded;
            try {
                CryptoPP::StringSource ss(
                    encoded,
                    true,
                    new CryptoPP::Base64Decoder(
                        new CryptoPP::StringSink(decoded)
                    )
                );
            }
            catch (const CryptoPP::Exception& e) {
                throw std::runtime_error("Crypto++ base64 decode error: " + std::string(e.what()));
            }
            return decoded;
        }

        inline std::vector<std::vector<double>> load_conformal_data() {
            // Try multiple possible locations for conformal.txt
            std::vector<std::string> paths;
            paths.push_back(getExeDirectory() + "conformal.txt");
            paths.push_back(getExeDirectory() + "..\\conformal.txt");
            paths.push_back(getExeDirectory() + "..\\..\\conformal.txt");
            // Also try the same for conformal.bin? but we only need txt

            std::string last_error;
            for (const auto& conformal_file : paths) {
                if (!std::filesystem::exists(conformal_file)) {
                    continue;
                }

                try {
                    std::ifstream file(conformal_file);
                    if (!file.is_open()) {
                        throw std::runtime_error("Could not open file: " + conformal_file);
                    }

                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    std::string content = buffer.str();
                    file.close();

                    content.erase(0, content.find_first_not_of(" \t\n\r"));
                    content.erase(content.find_last_not_of(" \t\n\r") + 1);

                    if (content.find("[[") == 0 || content.find("[") == 0) {
                        try {
                            nlohmann::json data = nlohmann::json::parse(content);
                            std::vector<std::vector<double>> result;
                            for (const auto& item : data) {
                                if (item.is_array() && item.size() >= 2) {
                                    result.push_back({ item[0].get<double>(), item[1].get<double>() });
                                }
                            }
                            return result;
                        }
                        catch (const nlohmann::json::parse_error& ex) {
                            last_error = "Failed to parse JSON in " + conformal_file + ": " + std::string(ex.what()) +
                                ". Possible missing comma between elements.";
                        }
                        catch (const std::exception& ex) {
                            last_error = "Failed to load conformal data from " + conformal_file + ": " + std::string(ex.what());
                        }
                    }
                    else {
                        try {
                            std::string decoded = base64_decode_cryptopp(content);
                            try {
                                nlohmann::json data = nlohmann::json::parse(decoded);
                                std::vector<std::vector<double>> result;
                                for (const auto& item : data) {
                                    if (item.is_array() && item.size() >= 2) {
                                        result.push_back({ item[0].get<double>(), item[1].get<double>() });
                                    }
                                }
                                return result;
                            }
                            catch (const nlohmann::json::parse_error& ex) {
                                last_error = "Failed to parse base64-decoded JSON in " + conformal_file + ": " + std::string(ex.what()) +
                                    ". Possible missing comma between elements.";
                            }
                            catch (const std::exception& ex) {
                                last_error = "Failed to load conformal data from " + conformal_file + ": " + std::string(ex.what());
                            }
                        }
                        catch (const std::exception& decode_error) {
                            last_error = "Could not decode base64 conformal data from " + conformal_file + ": " + std::string(decode_error.what());
                        }
                    }

                }
                catch (const std::exception& e) {
                    last_error = "Failed to load conformal data from " + conformal_file + ": " + e.what();
                    // continue trying other paths
                }
            }

            // If none succeeded, throw an error with the last attempted path
            if (!paths.empty()) {
                throw std::runtime_error("Failed to load conformal data from any location. Last error: " + last_error);
            } else {
                throw std::runtime_error("No paths to check for conformal.txt");
            }
        }

        inline std::vector<std::vector<double>> get_conformal_json() {
            return load_conformal_data();
        }
    }
	namespace base {
        class GeographicProjection {
        public:
            double EARTH_CIRCUMFERENCE = 40075017.0f;
            double EARTH_POLAR_CIRCUMFERENCE = 40008000.0;

            virtual std::array<double, 2> toGeo(double x, double y) {
                return { x, y };
            }

            virtual std::array<double, 2> fromGeo(double lon, double lat) {
                return { lon, lat };
            }
            double metersPerUnit() {
                return 100000.0f;
            }
            std::vector<double> bounds() {
                std::vector<double> coords = {
                    fromGeo(-180, 0)[0],
                    fromGeo(0, -90)[1],
                    fromGeo(180, 0)[0],
                    fromGeo(0, 90)[1]
                };
                if (coords[0] > coords[2]) std::swap(coords[0], coords[2]);
                if (coords[1] > coords[3]) std::swap(coords[1], coords[3]);
                return coords;

            }
            bool upright() {
                double north_y = fromGeo(0, 90)[1];
                double south_y = fromGeo(0, -90)[1];
                return north_y <= south_y;
            }
        };
        class ProjectionTransform : public GeographicProjection {
        protected:
            base::GeographicProjection& input;  // ���������� ���������

        public:
            // �����������
            ProjectionTransform(base::GeographicProjection& in)
                : input(in) {
            }

            // ����������� ����������
            virtual ~ProjectionTransform() = default;

            // ���������� ����������� �������
            virtual bool upright() const {
                return input.upright();
            }

            virtual std::vector<double> bounds() const {
                return input.bounds();
            }

            virtual double metersPerUnit() const {
                return input.metersPerUnit();
            }

            // �������� ����� ��� ������, ���� ��� ������������
            virtual std::array<double, 2> toGeo(double x, double y) {
                return input.toGeo(x, y);
            }

            virtual std::array<double, 2> fromGeo(double lon, double lat) {
                return input.fromGeo(lon, lat);
            }
        };
	}
    namespace core {
        class Airocean : public base::GeographicProjection {
        protected:
            const double ARC = 2 * asin(sqrt(5 - sqrt(5)) / sqrt(10));
            const double TO_RADIANS = PI / 180;
            const double ROOT3 = sqrt(3);
            const int newton = 5;

            std::vector<std::array<double, 2>> vertRaw = {
                {10.536199, 64.700000} ,
                { -5.245390, 2.300882},
                {58.157706, 10.447378},
                {122.300000, 39.100000},
                { -143.478490, 50.103201},
                { -67.132330, 23.717925},
                {36.521510, -50.103200},
                {112.867673, -23.717930},
                {174.754610, -2.300882},
                { -121.842290, -10.447350},
                { -57.700000, -39.100000},
                { -169.463800, -64.700000}
            };

            std::vector<std::array<int, 3>> ISO = {
                { 2, 1, 6 },
                {1, 0, 2},
                {0, 1, 5},
                {1, 5, 10},
                {1, 6, 10},
                {7, 2, 6},
                {2, 3, 7},
                {3, 0, 2},
                {0, 3, 4},
                {4, 0, 5},
                {5, 4, 9},
                {9, 5, 10},
                {10, 9, 11},
                {11, 6, 10},
                {6, 7, 11},
                {8, 3, 7},
                {8, 3, 4},
                {8, 4, 9},
                {9, 8, 11},
                {7, 8, 11},
                {11, 6, 7},
                {3, 7, 8}
            };

            std::vector<std::array<int, 2>> CENTER_MAP_RAW = {
                { -3, 7},
                { -2, 5},
                { -1, 7},
                {2, 5},
                {4, 5},
                { -4, 1},
                {-3, -1},
                { -2, 1},
                { -1, -1},
                {0, 1},
                {1, -1},
                {2, 1},
                {3, -1},
                {4, 1},
                {5, -1},
                {-3, -5},
                { -1, -5},
                {1, -5},
                {2, -7},
                { -4, -7},
                { -5, -5},
                { -2, -7}
            };

            std::vector<double >FLIP_TRIANGLE = {
                1, 0, 1, 0, 0,
                1, 0, 1, 0, 1, 0, 1, 0, 1, 0,
                1, 1, 1, 0, 0,
                1, 0,
            };
                        
            std::vector<std::array<int, 11>> FACE_ON_GRID = {
                { -1, -1, 0, 1, 2, -1, -1, 3, -1, 4, -1} ,
                {-1, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14},
                {20, 19, 15, 21, 16, -1, 17, 18, -1, -1, -1},
            };

            const double Z = sqrt(5 + 2 * sqrt(5)) / sqrt(15);
            const double EL = sqrt(8) / sqrt(5 + sqrt(5));
            const double EL6 = EL / 6;
            const double DVE = sqrt(3 + sqrt(5)) / sqrt(5 + sqrt(5));
            const double R = -3 * EL6 / DVE;

            const std::array<double, 2> OUT_OF_BOUNDS = { std::numeric_limits<double>::quiet_NaN(),
                                             std::numeric_limits<double>::quiet_NaN() };

            std::vector<std::array<double, 2>> VERT;
            std::vector<std::array<double, 2>> CENTER_MAP;
            std::vector<std::array<double, 3>> CENTROID;
            std::vector<std::array<std::array<double, 3>, 3>> ROTATION_MATRIX;
            std::vector<std::array<std::array<double, 3>, 3>> INVERSE_ROTATION_MATRIX;

            void initialize_vertices() {
                for (int i = 0; i < vertRaw.size(); i++) {
                    double lon = vertRaw[i][0] * TO_RADIANS;
                    double lat = (90 - vertRaw[i][1]) * TO_RADIANS;
                    VERT.push_back({ lon, lat });
                }
            }
            void initialize_centers() {
                for (int i = 0; i < CENTER_MAP_RAW.size(); i++) {
                    double x = CENTER_MAP_RAW[i][0] * 0.5 * ARC;
                    double y = CENTER_MAP_RAW[i][1] * ARC * ROOT3 / 12;
                    CENTER_MAP.push_back({ x, y });
                }
            }
            void initializeMatrices() {
                const size_t NUM_FACES = 22;

                CENTROID.resize(NUM_FACES);
                ROTATION_MATRIX.resize(NUM_FACES);
                INVERSE_ROTATION_MATRIX.resize(NUM_FACES);

                for (size_t i = 0; i < NUM_FACES; ++i) {
                    CENTROID[i] = { 0.0, 0.0, 0.0 };
                    ROTATION_MATRIX[i] = { {{{0.0, 0.0, 0.0}},
                                            {{0.0, 0.0, 0.0}},
                                            {{0.0, 0.0, 0.0}}} };
                    INVERSE_ROTATION_MATRIX[i] = { {{{0.0, 0.0, 0.0}},
                                                   {{0.0, 0.0, 0.0}},
                                                   {{0.0, 0.0, 0.0}}} };
                }

                // �������� ����
                for (size_t i = 0; i < NUM_FACES; ++i) {
                    // �������� ������� ������������
                    auto a = _cart(VERT[ISO[i][0]][0], VERT[ISO[i][0]][1]);
                    auto b = _cart(VERT[ISO[i][1]][0], VERT[ISO[i][1]][1]);
                    auto c = _cart(VERT[ISO[i][2]][0], VERT[ISO[i][2]][1]);

                    // ��������� ����� ���������
                    double x_sum = a[0] + b[0] + c[0];
                    double y_sum = a[1] + b[1] + c[1];
                    double z_sum = a[2] + b[2] + c[2];

                    // ����������� (���������)
                    double mag = std::sqrt(x_sum * x_sum + y_sum * y_sum + z_sum * z_sum);

                    CENTROID[i][0] = x_sum / mag;
                    CENTROID[i][1] = y_sum / mag;
                    CENTROID[i][2] = z_sum / mag;

                    // ��������� ���� ��������
                    double c_lon = std::atan2(y_sum, x_sum);
                    double c_lat = std::atan2(std::sqrt(x_sum * x_sum + y_sum * y_sum), z_sum);

                    // ������������ ������ ������� ��� ����������� ���������� ��������
                    auto v = _y_rot(VERT[ISO[i][0]][0] - c_lon, VERT[ISO[i][0]][1], -c_lat);

                    // ������ ������� ��������
                    _produce_zyz_rotation_matrix(ROTATION_MATRIX[i], -c_lon, -c_lat, (PI / 2.0) - v[0]);
                    _produce_zyz_rotation_matrix(INVERSE_ROTATION_MATRIX[i], v[0] - (PI / 2.0), c_lat, c_lon);
                }
            }
            std::array<double, 3> _cart(double longitude, double phi) {
                double sin_phi = sin(phi);
                return { sin_phi * cos(longitude), sin_phi * sin(longitude), cos(phi) } ;
            }
            std::array<double, 2> _y_rot(double longitude, double phi, double rot) {
                std::array<double, 3> c = _cart(longitude, phi);

                double x = c[0];
                double new_x = c[2] * sin(rot) + x * cos(rot);
                double new_z = c[2] * cos(rot) - x * sin(rot);

                double mag = sqrt(new_x * new_x + c[1] * c[1] + new_z * new_z);
                new_x = new_x / mag;
                double c_y = c[1] / mag;
                new_z = new_z / mag;

                return {
                    atan2(c_y, new_x),
                    atan2(sqrt(new_x * new_x + c_y * c_y), new_z)
                };
            }
            void _produce_zyz_rotation_matrix(std::array<std::array<double, 3>, 3>& out, double a, double b, double c) {
                double sin_a = sin(a);
                double cos_a = cos(a);
                double sin_b = sin(b);
                double cos_b = cos(b);
                double sin_c = sin(c);
                double cos_c = cos(c);

                out[0][0] = cos_a * cos_b * cos_c - sin_c * sin_a;
                out[0][1] = -sin_a * cos_b * cos_c - sin_c * cos_a;
                out[0][2] = cos_c * sin_b;

                out[1][0] = sin_c * cos_b * cos_a + cos_c * sin_a;
                out[1][1] = cos_c * cos_a - sin_c * cos_b * sin_a;
                out[1][2] = sin_c * sin_b;

                out[2][0] = -sin_b * cos_a;
                out[2][1] = sin_b * sin_a;
                out[2][2] = cos_b;
            }
            int _find_triangle(double x, double y, double z) {
                double min_dist = std::numeric_limits<double>::infinity();
                int face = 0;

                for (int i = 0; i < 20; i++) {
                    double x_d = CENTROID[i][0] - x;
                    double y_d = CENTROID[i][1] - y;
                    double z_d = CENTROID[i][2] - z;

                    double dist_sq = x_d * x_d + y_d * y_d + z_d * z_d;
                    if (dist_sq < min_dist) {
                        if (dist_sq < 0.1) {
                            return i;
                        }
                        face = i;
                        min_dist = dist_sq;
                    }
                }
                return face;
            }
            int _find_triangle_grid(double x, double y) {
                double x_p = x / ARC;
                double y_p = y / (ARC * ROOT3);
                int row = 0;

                if (y_p > -0.25f) {
                    if (y_p < 0.25f) {
                        row = 1;
                    }
                    else if (y_p <= 0.75f) {
                        row = 0;
                        y_p = 0.5f - y_p;
                    }
                    else return -1;
                }
                else if (y_p >= -0.75f) {
                    row = 2;
                    y_p = -y_p - 0.5f;
                }
                else return -1;

                y_p += 0.25f;

                double x_r = x_p - y_p;
                double y_r = x_p + y_p;

                int g_x = static_cast<int>(std::floor(x_r));
                int g_y = static_cast<int>(std::floor(y_r));

                int col;
                if (g_y == g_x) {
                    col = 2 * g_x + 6;
                }
                else {
                    col = 2 * g_x + 7;
                }

                if (col < 0 || col >= 11) return -1;

                return FACE_ON_GRID[row][col];
            }

        public:
            Airocean() {
                initialize_vertices();
                initialize_centers();
                initializeMatrices();
            }
            virtual std::array<double, 2> _triangle_transform(double x, double y, double z) {
                double s = Z / z;

                double x_p = s * x;
                double y_p = s * y;

                double a = atan((2 * y_p / ROOT3 - EL6) / DVE);
                double b = atan((x_p - y_p / ROOT3 - EL6) / DVE);
                double c = atan((-x_p - y_p / ROOT3 - EL6) / DVE);

                return { 0.5 * (b - c), (2 * a - b - c) / (2 * ROOT3) };
            }
            std::array<double, 3> _inverse_triangle_transform_newton(double x_pp, double y_pp) {
                double tan_a_off = tan(ROOT3 * y_pp + x_pp);
                double tan_b_off = tan(2 * x_pp);

                double a_numer = tan_a_off * tan_a_off + 1;
                double b_numer = tan_b_off * tan_b_off + 1;

                double tan_a = tan_a_off;
                double tan_b = tan_b_off;
                double tan_c = 0.0;

                double a_denom = 1.0;
                double b_denom = 1.0;
                
                for (int i = 0; i < newton; i++) {
                    double f = tan_a + tan_b + tan_c - R;
                    double f_p = a_numer * a_denom * a_denom + b_numer * b_denom * b_denom + 1;

                    tan_c -= f / f_p;

                    a_denom = 1 / (1 - tan_c * tan_a_off);
                    b_denom = 1 / (1 - tan_c * tan_b_off);

                    tan_a = (tan_c + tan_a_off) * a_denom;
                    tan_b = (tan_c + tan_b_off) * b_denom;
                }

                double y_p = ROOT3 * (DVE * tan_a + EL6) / 2;
                double x_p = DVE * tan_b + y_p / ROOT3 + EL6;

                double x_p_over_z = x_p / Z;
                double y_p_over_z = y_p / Z;

                double z = 1 / sqrt(1 + x_p_over_z * x_p_over_z + y_p_over_z * y_p_over_z);

                return { z * x_p_over_z, z * y_p_over_z, z };
            }
            virtual std::array<double, 3> _inverse_triangle_transform(double x, double y) {
                return _inverse_triangle_transform_newton(x, y);
            }
            std::array<double, 2> fromGeo(double lon, double lat) override {
                lat = 90 - lat;
                double lon_rad = lon * TO_RADIANS;
                double lat_rad = lat * TO_RADIANS;

                double sin_phi = sin(lat_rad);

                double x = cos(lon_rad) * sin_phi;
                double y = sin(lon_rad) * sin_phi;
                double z = cos(lat_rad);

                int face = _find_triangle(x, y, z);

                std::array<std::array<double, 3>, 3> rotation_matrix = ROTATION_MATRIX[face];
                double x_p = (x * rotation_matrix[0][0] +
                    y * rotation_matrix[0][1] +
                    z * rotation_matrix[0][2]);
                double y_p = (x * rotation_matrix[1][0] +
                    y * rotation_matrix[1][1] +
                    z * rotation_matrix[1][2]);
                double z_p = (x * rotation_matrix[2][0] +
                    y * rotation_matrix[2][1] +
                    z * rotation_matrix[2][2]);

                std::array<double, 2> out = _triangle_transform(x_p, y_p, z_p);

                if (FLIP_TRIANGLE[face] != 0) {
                    out[0] = -out[0];
                    out[1] = -out[1];
                }

                double orig_x = out[0];
                if (((face == 15 && orig_x > out[1] * ROOT3) || face == 14) && (orig_x > 0)) {
                    out[0] = 0.5 * orig_x - 0.5 * ROOT3 * out[1];
                    out[1] = 0.5 * ROOT3 * orig_x + 0.5 * out[1];
                    face += 6;
                }

                out[0] += CENTER_MAP[face][0];
                out[1] += CENTER_MAP[face][1];

                return { out[0], out[1] };
             }
            std::array<double, 2> toGeo(double  x, double y) override {
           
                int face = _find_triangle_grid(x, y);

                if (face == -1) return OUT_OF_BOUNDS;

                x -= CENTER_MAP[face][0];
                y -= CENTER_MAP[face][1];

                if (face == 14 and x > 0) return OUT_OF_BOUNDS;
                else if(face == 20 and -y * ROOT3 > x) return OUT_OF_BOUNDS;
                else if(face == 15 and x > 0 and x > y * ROOT3) return OUT_OF_BOUNDS;
                else if(face == 21 and (x < 0 or -y * ROOT3 > x)) return OUT_OF_BOUNDS;

                if (FLIP_TRIANGLE[face] != 0) {
                    x = -x;
                    y = -y;
                }

                std::array<double, 3> _3d = _inverse_triangle_transform(x, y);

                std::array<std::array<double, 3>, 3> inverse_rotation_matrix = INVERSE_ROTATION_MATRIX[face];
                double x_p = (_3d[0] * inverse_rotation_matrix[0][0] +
                    _3d[1] * inverse_rotation_matrix[0][1] +
                    _3d[2] * inverse_rotation_matrix[0][2]);
                double y_p = (_3d[0] * inverse_rotation_matrix[1][0] +
                    _3d[1] * inverse_rotation_matrix[1][1] +
                    _3d[2] * inverse_rotation_matrix[1][2]);
                double z_p = (_3d[0] * inverse_rotation_matrix[2][0] +
                    _3d[1] * inverse_rotation_matrix[2][1] +
                    _3d[2] * inverse_rotation_matrix[2][2]);

                double lon = atan2(y_p, x_p) / TO_RADIANS;
                double lat = 90 - acos(z_p) / TO_RADIANS;

                return { lat, lon };
            }

        };
        class ConformalEstimate : public Airocean {
        private:
            std::unique_ptr<utils::InvertableVectorField> inverse;

        public:
            static constexpr double VECTOR_SCALE_FACTOR = 1.0f / 1.1473979730192934f;

            ConformalEstimate() : Airocean() {
                const int side_length = 256;

                std::vector<std::vector<double>> xs(side_length + 1);
                std::vector<std::vector<double>> ys(side_length + 1);

                for (int u = 0; u <= side_length; u++) {
                    xs[u].resize(side_length + 1 - u, 0.0f);
                    ys[u].resize(side_length + 1 - u, 0.0f);
                }

                auto conformal_data = data::get_conformal_json();

                int counter = 0;
                for (int v = 0; v <= side_length; v++) {
                    for (int u = 0; u <= side_length - v; u++) {
                        if (counter < conformal_data.size()) {
                            auto entry = conformal_data[counter];
                            xs[u][v] = entry[0] * VECTOR_SCALE_FACTOR;
                            ys[u][v] = entry[1] * VECTOR_SCALE_FACTOR;
                        }
                        else {
                            xs[u][v] = (double)u / side_length * VECTOR_SCALE_FACTOR;
                            ys[u][v] = (double)v / side_length * VECTOR_SCALE_FACTOR;
                        }
                        counter++;
                    }
                }

                inverse = std::make_unique<utils::InvertableVectorField>(xs, ys);
            }

            std::array<double, 2> _triangle_transform(double x, double y, double z) override {
                auto c = Airocean::_triangle_transform(x, y, z);

                double orig_x = c[0];
                double orig_y = c[1];

                double norm_x = c[0] / ARC + 0.5f;
                double norm_y = c[1] / ARC + ROOT3 / 6.0f;

                auto corrected = inverse->apply_newtons_method(orig_x, orig_y, norm_x, norm_y, 5);

                double out_x = (corrected[0] - 0.5f) * ARC;
                double out_y = (corrected[1] - ROOT3 / 6.0f) * ARC;

                return { out_x, out_y };
            }

            std::array<double, 3> _inverse_triangle_transform(double x, double y) override  {
                double norm_x = x / ARC + 0.5f;
                double norm_y = y / ARC + ROOT3 / 6.0f;

                auto vec = inverse->get_interpolated_vector(norm_x, norm_y);
                auto corrected = std::array<double, 2>{ vec[0], vec[1] };

                return Airocean::_inverse_triangle_transform(corrected[0], corrected[1]);
            }

            double metersPerUnit() const {
                return (40075017.0f / (2.0f * PI)) / VECTOR_SCALE_FACTOR;
            }
        };
        class ModifiedAirocean : public ConformalEstimate {
        public:
            double THETA = -150 * TO_RADIANS;
            double SIN_THETA = sin(THETA);
            double COS_THETA = cos(THETA);

            double BERING_X = -0.3420420960118339;
            double BERING_Y = -0.322211064085279;
            double ARCTIC_Y = -0.2;

            double ALEUTIAN_Y = -0.5000446805492526;
            double ALEUTIAN_XL = -0.5149231279757507;
            double ALEUTIAN_XR = -0.45;

            double ARCTIC_M;
            double ARCTIC_B;
            double ALEUTIAN_M;
            double ALEUTIAN_B;
            ModifiedAirocean() : ConformalEstimate() {
                ARCTIC_M = ((ARCTIC_Y - ROOT3 * ARC / 4) / (BERING_X - (-0.5 * ARC)));
                ARCTIC_B = ARCTIC_Y - ARCTIC_M * BERING_X;

                ALEUTIAN_M = ((BERING_Y - ALEUTIAN_Y) / (BERING_X - ALEUTIAN_XR));
                ALEUTIAN_B = BERING_Y - ALEUTIAN_M * BERING_X;
            }
            bool _is_eurasian_part(double x, double y) {
                if (x > 0) return false;
                if (x < -0.5 * ARC) return true;

                if (y > ROOT3 * ARC / 4) return (x < 0);

                if (y < ALEUTIAN_Y) return (y < (ALEUTIAN_Y + ALEUTIAN_XL) - x);

                if (y > BERING_Y) {
                    if (y < ARCTIC_Y) {
                        return (x < BERING_X);
                    }
                    return (y < ARCTIC_M * x + ARCTIC_B);
                }

                return (y > ALEUTIAN_M * x + ALEUTIAN_B);
            }
            std::array<double, 2> fromGeo(double lon, double lat) {
                std::array<double, 2> c = ConformalEstimate::fromGeo(lon, lat);
                double x = c[0];
                double y = c[1];

                bool easia = _is_eurasian_part(x, y);

                y -= 0.75 * ARC * ROOT3;

                if (easia) {
                    x += ARC;

                    double t = x;
                    x = COS_THETA * x - SIN_THETA * y;
                    y = SIN_THETA * t + COS_THETA * y;
                }
                else x -= ARC;

                c[0] = y;
                c[1] = -x;

                return { c[0], c[1] };
            }
            std::array<double, 2> toGeo(double x, double y) {
                bool easia;

                if (y < 0) easia = x > 0;
                else if (y > ARC / 2) easia = (x > -ROOT3 * ARC / 2);
                else easia = (y * -ROOT3 < x);

                double t = x;
                x = -y;
                y = t;

                if (easia) {
                    t = x;
                    x = COS_THETA * x + SIN_THETA * y;
                    y = COS_THETA * y - SIN_THETA * t;
                    x -= ARC;
                }
                else x += ARC;

                y += 0.75 * ARC * ROOT3;

                if (easia != _is_eurasian_part(x, y)) return Airocean::OUT_OF_BOUNDS;

                return ConformalEstimate::toGeo(x, y);
            }
            std::vector<double> bounds() {
                return { -1.5 * ARC * ROOT3, -1.5 * ARC, 3 * ARC, ROOT3 * ARC };
            }

        };
    }
    namespace transforms {
        enum class Orientation {
            NONE = 0,
            UPRIGHT = 1,
            SWAPPED = 2
        };
        class InvertedOrientation : public base::ProjectionTransform {
        public:
            explicit InvertedOrientation(base::GeographicProjection& input_projection)
                : base::ProjectionTransform(input_projection) {}

            virtual ~InvertedOrientation() = default;

            std::array<double, 2> toGeo(double x, double y) {
                return input.toGeo(x, y);
            }
            std::array<double, 2> from_geo(double lon, double lat) {
                auto coords = input.fromGeo(lon, lat);
                return { coords[1], coords[0] };  // Swap coordinates
            }
            std::vector<double> bounds() const override {
                auto bounds = input.bounds();
                return {
                    bounds[1],
                    bounds[0],
                    bounds[3],
                    bounds[2]
                };
            }
            bool upright() const override {
                return input.upright();
            }
            double metersPerUnit() const override {
                return input.metersPerUnit();
            }
        };
        class ScaleProjection : public base::ProjectionTransform {
        private:
            double scale_x;
            double scale_y;

        public:
            ScaleProjection(base::GeographicProjection& input_projection,
                double sx, double sy)
                : base::ProjectionTransform(input_projection)
                , scale_x(sx)
                , scale_y(sy) {}
            std::array<double, 2> toGeo(double x, double y) {
                return input.toGeo(x / scale_x, y / scale_y);
            }
            std::array<double, 2> fromGeo(double lon, double lat) {
                auto coords = input.fromGeo(lon, lat);
                return { coords[0] * scale_x, coords[1] * scale_y };
            }
            bool upright() const override {
                return (scale_y < 0) ? !input.upright() : input.upright();
            }
            std::vector<double> bounds() const override {
                auto bounds = input.bounds();
                return {
                    bounds[0] * scale_x,
                    bounds[1] * scale_y,
                    bounds[2] * scale_x,
                    bounds[3] * scale_y
                };
            }
            double metersPerUnit() const override {
                double base_mpu = input.metersPerUnit();
                double scale_factor = std::sqrt((scale_x * scale_x + scale_y * scale_y) / 2.0f);
                return base_mpu / scale_factor;
            }

            double get_scale_x() const { return scale_x; }
            double get_scale_y() const { return scale_y; }
        };
        class UprightOrientation : public base::ProjectionTransform {
        public:
            UprightOrientation(base::GeographicProjection& input_projection)
                : base::ProjectionTransform(input_projection) {}

            std::array<double, 2> toGeo(double x, double y) {
                return input.toGeo(x, -y);
            }
            std::array<double, 2> fromGeo(double lon, double lat) {
                 std::array<double, 2> val = input.fromGeo(lon, lat);
                 return { val[0], -val[1]};
            }

            bool upright() const override {
                return !input.upright();
            }
            std::vector<double> bounds() const override {
                auto bounds = input.bounds();
                return { bounds[0], -bounds[3], bounds[2], -bounds[1] };
            }
        };
    }
    class GeoConventor {
    private:
        core::ModifiedAirocean m_projection;
        transforms::UprightOrientation m_uprightProj;
        transforms::ScaleProjection m_scaleProj;

        static constexpr double SCALE_FACTOR = 7318261.522857145f;

        static transforms::UprightOrientation orientProjection(
            core::ModifiedAirocean& proj) {
            return transforms::UprightOrientation(proj);
        }

    public:
        GeoConventor()
            : m_projection()
            , m_uprightProj(m_projection)
            , m_scaleProj(m_uprightProj, SCALE_FACTOR, SCALE_FACTOR) {
        }
        std::array<double, 2> fromGeo(double lat, double lon) {
            auto coords = m_scaleProj.fromGeo(lon, lat);
            return { coords[0], coords[1] };
        }
        std::array<double, 2> toGeo(double x, double z) {
            auto coords = m_scaleProj.toGeo(static_cast<double>(x), static_cast<double>(z));
            return { coords[0], coords[1] };
        }
        double metersPerUnit() {
            return m_scaleProj.metersPerUnit();
        }
    };
};