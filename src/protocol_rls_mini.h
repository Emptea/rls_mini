#ifndef protocol_rls_mini_H
#define protocol_rls_mini_H

#include <cmath>
#include <cstdint>
#include <piethernet.h>

class Protocol_RLS_Mini {
public:
#pragma pack(push, 1)

	enum control_point {
		CTRL       = 0x00,
		PHD        = 0x01,
		FAP        = 0x02,
		LOU        = 0x03,
		SF         = 0x04,
		KN         = 0x05, // TP_MAX
		AD         = 0x06, // TP_FFT for now
		APU        = 0x07, // NIIRT tp ends here
		CUT        = 0x08, // our tp starts here
		DDR        = 0x09,
		FIND       = 0x0A,
		RANK       = 0x0B,
		FAP_COEFFS = 0x0C,
		WEIGHT_OUT = 0x0D,
		MTI        = 0x0F,
	};

	struct Header PIMETA(no-stream) {
		static constexpr bool Broadcast = false;
		Header(uint8_t t = 0, uint8_t c = 0) {
			msg_type = t;
			msg_code = c;
		}
		uint8_t msg_code; // Код операции
		uint8_t msg_type; // Тип сообщения
	};


	// RR

	struct RR_Zapros: public Header PIMETA(dest=rr, port=5001, no-stream) {
		static constexpr int16_t Port = 5001;
		static constexpr int16_t Type = 1;
		static constexpr int16_t Code = 0;
		RR_Zapros(): Header(Type, Code) {}
	};


	struct RR_Vr: public Header PIMETA(dest=rr, port=5001, no-stream) {
		static constexpr int16_t Port = 5001;
		static constexpr int16_t Type = 1;
		static constexpr int16_t Code = 1;
		RR_Vr(): Header(Type, Code) {}
		uint16_t par = 0; // 0 - выключить вращение и имитатор вращения, 1 - включить вращение, 2 - включить имитатор вращения
	};


	struct RR_Izl: public Header PIMETA(dest=rr, port=5001, no-stream) {
		static constexpr int16_t Port = 5001;
		static constexpr int16_t Type = 1;
		static constexpr int16_t Code = 2;
		RR_Izl(): Header(Type, Code) {}
		uint16_t par = 0; // 0 - выключить излучение, 1 - включить излучение
	};


	struct RR_Ant: public Header PIMETA(dest=rr, port=5001, no-stream) {
		static constexpr int16_t Port = 5001;
		static constexpr int16_t Type = 1;
		static constexpr int16_t Code = 3;
		RR_Ant(): Header(Type, Code) {}
		uint16_t par = 0; // 0 - антенна отключена, 1 - антенна подключена
	};


	struct RR_TTek: public Header PIMETA(dest=rr, port=5001, no-stream) {
		static constexpr int16_t Port = 5001;
		static constexpr int16_t Type = 1;
		static constexpr int16_t Code = 4;
		RR_TTek(): Header(Type, Code) {}
		uint16_t time_msw = 0;
		uint16_t time_lsw = 0;
		double getSeconds() const { return ((uint32_t(time_msw) << 16) | time_lsw) * timeLSB; }
	};


	struct RR_AzPopr: public Header PIMETA(dest=rr, port=5001, no-stream) {
		static constexpr int16_t Port = 5001;
		static constexpr int16_t Type = 1;
		static constexpr int16_t Code = 5;
		RR_AzPopr(): Header(Type, Code) {}
		int16_t daz = 0;
		void setDegrees(double v) { daz = v / degLSB; }
		double getDegrees() const { return daz * degLSB; }
	};


	struct RR_DPopr_POI: public Header PIMETA(dest=rr, port=5001, no-stream) {
		static constexpr int16_t Port = 5001;
		static constexpr int16_t Type = 1;
		static constexpr int16_t Code = 6;
		RR_DPopr_POI(): Header(Type, Code) {}
		int16_t dd = 0; // 1 м
	};


	struct RR_AzPopr_POI: public Header PIMETA(dest=rr, port=5001, no-stream) {
		static constexpr int16_t Port = 5001;
		static constexpr int16_t Type = 1;
		static constexpr int16_t Code = 7;
		RR_AzPopr_POI(): Header(Type, Code) {}
		int16_t daz = 0;
		void setDegrees(double v) { daz = v / degLSB; }
		float getDegrees() const { return daz * degLSB; }
	};


	struct RR_Kvit: public Header PIMETA(dest=all, port=5000, no-stream) {
		static constexpr bool Broadcast = true;
		static constexpr int16_t Port   = 5000;
		static constexpr int16_t Type   = 1;
		static constexpr int16_t Code   = 0;
		RR_Kvit(): Header(Type, Code) {}
		uint8_t ispr_kan = 0;
		union {
			uint8_t bits = 0;
			struct {
				uint8_t _reserve: 1;
				uint8_t ant     : 1;
				uint8_t izl     : 1;
				uint8_t kuizl   : 1;
				uint8_t vr      : 2;
				uint8_t kuvr    : 2;
			};
		};
		int16_t daz     = 0;
		int16_t dd_poi  = 0; // 1 м
		int16_t daz_poi = 0;
		void setDegreesDaz(double v) { daz = v / degLSB; }
		double getDegreesDaz() const { return daz * degLSB; }
		void setDegreesDazPOI(double v) { daz_poi = v / degLSB; }
		double getDegreesDazPOI() const { return daz_poi * degLSB; }
	};


	// POI

	struct POI_Zapros: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static constexpr int16_t Port = 5101;
		static constexpr int16_t Type = 2;
		static constexpr int16_t Code = 0;
		POI_Zapros(): Header(Type, Code) {}
	};


	struct POI_Shtat: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static constexpr int16_t Port = 5101;
		static constexpr int16_t Type = 2;
		static constexpr int16_t Code = 255;
		POI_Shtat(): Header(Type, Code) {}
	};


	struct POI_SDC: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static constexpr int16_t Port = 5101;
		static constexpr int16_t Type = 2;
		static constexpr int16_t Code = 1;
		POI_SDC(): Header(Type, Code) {}
		uint16_t par = 0; // 0 - выключить СДЦ, 1 - включить СДЦ
	};


	struct POI_DSA: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static constexpr int16_t Port = 5101;
		static constexpr int16_t Type = 2;
		static constexpr int16_t Code = 2;
		POI_DSA(): Header(Type, Code) {}
		uint16_t par  = 0; // 0 - выключить ДСА, 1 - включить ДСА
		uint16_t vr_n = 0; // Vr начала полосы режекции, 1 м/с
		uint16_t vr_k = 0; // Vr конца  полосы режекции, 1 м/с
	};


	struct POI_APU: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static constexpr int16_t Port = 5101;
		static constexpr int16_t Type = 2;
		static constexpr int16_t Code = 3;
		POI_APU(): Header(Type, Code) {}
		uint16_t k1 = 0; // Коэффициент порога 1, цмр 0.5 дБ
		uint16_t k2 = 0; // Коэффициент порога 2, цмр 0.5 дБ
		void setLevelk1(uint32_t v) { k1 = (uint16_t)(10 * log10((double)v)); }
		double getLevelk1() const { return (pow(10, k1) / 10); }
		void setLevelk2(uint32_t v) { k2 = (uint16_t)(10 * log10((double)v)); }
		double getLevelk2() const { return (pow(10, k2) / 10); }
	};


	struct POI_Zona: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static constexpr int16_t Port = 5101;
		static constexpr int16_t Type = 2;
		static constexpr int16_t Code = 4;
		POI_Zona(): Header(Type, Code) {}
		uint8_t k1 = 0; // Код строба расчета зоны
		uint8_t _reserve;
		uint16_t k2 = 0; // Код канала расчета зоны
	};


	struct POI_Kan: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static constexpr int16_t Port = 5101;
		static constexpr int16_t Type = 2;
		static constexpr int16_t Code = 5;
		POI_Kan(): Header(Type, Code) {}
		uint8_t _reserve;
		uint8_t kan = 0; // Подключенные каналы
	};


	struct POI_Kvit: public Header PIMETA(dest=soi, port=5100, no-stream) {
		static constexpr int16_t Port = 5100;
		static constexpr int16_t Type = 2;
		static constexpr int16_t Code = 0;
		POI_Kvit(): Header(Type, Code) {}
		union {
			uint8_t bits = 0;
			struct {
				uint8_t _reserve0: 6;
				uint8_t sdc      : 1;
				uint8_t dsa      : 1;
			};
		};
		uint8_t kan       = 0; // Подключенные каналы
		uint16_t dsa_vr_n = 0;
		uint16_t dsa_vr_k = 0;
		uint16_t apu_k1   = 0;
		uint16_t apu_k2   = 0;
		uint8_t zona_k1   = 0;
		uint8_t _reserve1;
		uint16_t zona_k2 = 0;
		void setLevelApuk1(uint32_t v) { apu_k1 = uint16_t(round(10.0 * log10(v) / levelApukLSB)); }
		float getLevelApuk1() const { return pow(10.0, apu_k1 * levelApukLSB / 10.0); }
		void setLevelApuk2(uint32_t v) { apu_k2 = uint16_t(round(10.0 * log10(v) / levelApukLSB)); }
		float getLevelApuk2() const { return std::pow(10.0, apu_k2 * levelApukLSB / 10.0); }
	};


	struct POI_TK_Zapros: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static constexpr int16_t Port = 5101;
		static constexpr int16_t Type = 3;
		static constexpr int16_t Code = 0;
		POI_TK_Zapros(): Header(Type, Code) {} // 0 - запрос состояния системы ПОИ, 1 - запрос контрольной точки
		uint16_t kt   = 0;                     // Код контрольной точки
		uint16_t nkan = 0;                     // Номер канала
		uint16_t reg_takt = 0; // Количество регистрируемых тактов: 0 - запрос состояния системы ПОИ, 1 - запрос контрольной точки
	};


	struct POI_TK_Kvit: public Header PIMETA(dest=soi, port=5100, no-stream) {
		static constexpr int16_t Port = 5100;
		static constexpr int16_t Type = 3;
		static constexpr int16_t Code = 0;
		POI_TK_Kvit(): Header(Type, Code) {} // 0 - состояние системы ПОИ, 1 - контрольная точка
		uint16_t nw = 0;                     // Количество слов данных
		PIVector<uint16_t> words;

		template<typename T>
		void setData(const PIVector<T> & d) {
			setDataInternal(d.data(), d.size());
		}
		template<typename T>
		void setData(const PIVector<T> & d, int cnt) {
			setDataInternal(d.data(), cnt);
		}
		template<typename T>
		void setData(const PIDeque<T> & d) {
			setDataInternal(d.data(), d.size());
		}
		template<typename T>
		void setData(const T * d, int cnt) {
			setDataInternal(d, cnt);
		}

	private:
		template<typename T>
		void setDataInternal(const T * d, int cnt) {
			if (!d || cnt <= 0)
				words.clear();
			else
				words = PIVector<uint16_t>((uint16_t *)d, cnt * sizeof(T) / sizeof(uint16_t));
		}
	};


	// CIT

	struct CIT_Zapros: public Header PIMETA(dest=it, port=5401, no-stream) {
		static constexpr int16_t Port = 5401;
		static constexpr int16_t Type = 9;
		static constexpr int16_t Code = 0;
		CIT_Zapros(): Header(Type, Code) {}
	};


	struct ImitatorTarget PIMETA(no-stream) {
		uint8_t reg  = 0; // 0 - POA, 1 - PBA
		uint8_t on   = 0; // 0 - Выключена, 1 - Включен
		uint16_t az  = 0; // Азимут, без знака, цмр 0.0054931640625 гр
		uint16_t D   = 0; // Дальность, без знака, цмр 1 м
		int16_t um   = 0; // Угол места, в доп.коде, цмр 0.1 град
		int16_t vr   = 0; // Радиальная скорость, в доп.коде, цмр 1 м/с
		uint16_t osl = 0; // Ослабление, без знака, цмр 0.5 дБ
		void setDegreesAz(float v) { az = static_cast<uint16_t>(std::lround(v / degLSB)); }
		float getDegreesAz() const { return az * degLSB; }
		void setDegreesUm(float v) { um = static_cast<int16_t>(std::lround(v / 0.1f)); }
		float getDegreesUm() const { return um * 0.1f; }
		void setMeters(float v) { D = static_cast<uint16_t>(std::lround(v)); }
		float getMeters() const { return D; }
		void setVelocity(float v) { vr = static_cast<int16_t>(std::lround(v)); }
		float getVelocity() const { return vr; }
		void setAttenuation(float v) { osl = static_cast<uint16_t>(std::lround(v / 0.5f)); }
		float getAttenuation() const { return osl * 0.5f; }
	};


	struct CIT_KU: public Header PIMETA(dest=it, port=5401, no-stream) {
		static constexpr int16_t Port = 5401;
		static constexpr int16_t Type = 9;
		static constexpr int16_t Code = 1;
		CIT_KU(): Header(Type, Code) {}
		ImitatorTarget it1;
		ImitatorTarget it2;
	};


	struct CIT_Kvit: public Header PIMETA(dest=soi, port=5400, no-stream) {
		static constexpr int16_t Port = 5400;
		static constexpr int16_t Type = 9;
		static constexpr int16_t Code = 0;
		CIT_Kvit(): Header(Type, Code) {}
		ImitatorTarget it1;
		ImitatorTarget it2;
	};


	// PI

	struct PI: public Header PIMETA(dest=all, port=5200, no-stream) {
		static constexpr bool Broadcast = true;
		static constexpr int16_t Port   = 5200;
		static constexpr int16_t Type   = 4;
		static constexpr int16_t Code   = 0;
		PI(): Header(Type, Code) {}
		uint16_t aztek    = 0; // Азимут текущий, незадержанный в ПОИ
		uint16_t time_msw = 0; // Время, задержанное в обработке
		uint16_t time_lsw = 0; // Время, задержанное в обработке
		uint16_t az       = 0; // Азимут, задержанный в обработке
		uint16_t zona     = 0; // Дальность отметки "Зона"
		uint16_t nes      = 0; // Количество эхосигналов
		                       // Дальность эхосигнала
		                       // ...
		                       // uint16_t[nes]
		void setDegreesAzTek(double v) { aztek = v / degLSB; }
		void setDegreesAz(double v) { az = v / degLSB; }
		void setSeconds(double s) {
			uint32_t t = uint32_t(std::round(s / timeLSB));
			time_msw   = uint16_t(t >> 16);
			time_lsw   = uint16_t(t);
		}
	};


	struct KTA_VO: public Header PIMETA(dest=all, port=5202, no-stream) {
		static constexpr bool Broadcast = true;
		static constexpr int16_t Port   = 5202;
		static constexpr int16_t Type   = 4;
		static constexpr int16_t Code   = 1;
		KTA_VO(): Header(Type, Code) {}
		uint16_t N     = 0; // Номер КТА
		uint32_t T     = 0; // Время локации
		uint16_t az    = 0; // Азимут
		int16_t um     = 0; // Угол места
		uint16_t D     = 0; // Дальность, 1 м
		uint16_t porog = 0; // Порог обнаружения, 0.5 дБ
		uint16_t sp    = 0; // Отношение сигнал/порог, 0.5 дБ
		uint16_t amp   = 0; // Амплитуда сигнала цели, 0.5 дБ
		int16_t vr     = 0; // Радиальная скорость, 1 м/с
		union {
			uint8_t bits = 0;
			struct {
				uint8_t _reserve0: 6;
				uint8_t tn       : 1; // Признак тренажной цели
				uint8_t dum      : 1; // Признак достоверности измерения угла места цели
			};
		};
		uint8_t _reserve1 = 0;
		void setDegreesAz(double v) { az = v / degLSB; }
		double getDegreesAz() const { return az * degLSB; }
		void setDegreesUm(double v) { um = v / 0.1; }
		double getDegreesUm() const { return um * 0.1; }
		void setSeconds(double s) { T = s / timeLSB; }
		double getSeconds() const { return T * timeLSB; }
	};


	struct TRVO: public Header PIMETA(dest=all, port=5300, no-stream) {
		static constexpr int16_t Port = 5300;
		static constexpr int16_t Type = 5;
		static constexpr int16_t Code = -1;
		TRVO(): Header(Type, 0) {}
		uint16_t N  = 0; // Номер КТА
		uint32_t ID = 0; // Идентификатор трассы
		uint32_t T  = 0; // Время локации
		uint16_t az = 0; // Азимут
		uint16_t D  = 0; // Дальность, 1 м
		int16_t um  = 0; // Угол места
		int16_t X   = 0; // 1 м
		int16_t Y   = 0; // 1 м
		int16_t H   = 0; // 1 м
		int16_t VX  = 0; // 1 м/с
		int16_t VY  = 0; // 1 м/с
		int16_t VH  = 0; // 1 м/с
		int16_t VK  = 0; // Курсовая скорость
		int16_t K   = 0; // Курс
		union {
			uint8_t bits0 = 0;
			struct {
				uint8_t _reserve0: 3;
				uint8_t mh       : 1; // Признак наличия маневра по высоте
				uint8_t mk       : 1; // Признак наличия маневра по курсу
				uint8_t pv: 1; // Признак выдачи координат и времени: 0 - на момент выдачи, 1 - на момент локации
				uint8_t tn : 1; // Признак тренажной цели
				uint8_t dum: 1; // Признак достоверности измерения угла места цели
			};
		};
		union {
			uint8_t bits1 = 0;
			struct {
				uint8_t klass: 5; // Класс цели
				uint8_t sopr : 3; // Признак сопровождения:
				                  // 0 - новая трасса
				                  // 1 - обновление координат
				                  // 2 - экстраполяция (пропуск в обнаружении)
				                  // 3 - сброс с сопровождения
			};
		};
		PIVector<uint16_t> KTA;
		// KTA_N1
		// ...
		// uint16_t[msg_code]

		void setDegreesAz(double v) { az = v / degLSB; }
		double getDegreesAz() const { return az * degLSB; }
		void setDegreesKurs(double v) { K = v / degLSB; }
		double getDegreesKurs() const { return K * degLSB; }
		void setDegreesUm(double v) { um = v / 0.1; }
		double getDegreesUm() const { return um * 0.1; }
		void setSeconds(double s) { T = s / timeLSB; }
		double getSeconds() const { return T * timeLSB; }
	};


	struct TRETA: public TRVO PIMETA(dest=all, port=5300, no-stream) {
		static constexpr int16_t Port = 5300;
		static constexpr int16_t Type = 10;
		static constexpr int16_t Code = -1;
		TRETA() { msg_type = 10; }
	};


	struct STRSOPR: public Header PIMETA(dest=reg, port=5300, no-stream) {
		static constexpr int16_t Port = 5300;
		static constexpr int16_t Type = 11;
		static constexpr int16_t Code = 0;
		STRSOPR(): Header(Type, Code) {}
		uint16_t N       = 0; // Номер трассы
		uint16_t az      = 0; // Азимут центра строба
		uint16_t D       = 0; // Дальность центра строба, 1 м
		int16_t um       = 0; // Угол места центра строба
		int16_t X        = 0; // Координата Х центра строба, 1 м
		int16_t Y        = 0; // Координата Y центра строба, 1 м
		int16_t H        = 0; // Координата H центра строба, 1 м
		uint16_t Daz     = 0; // Полуширина по азимуту строба по ошибкам
		uint16_t DD      = 0; // Полуширина по дальности строба по ошибкам
		int16_t Dum      = 0; // Полуширина по углу места строба по ошибкам
		int16_t DX       = 0; // Полуширина по X строба по ошибкам
		int16_t DY       = 0; // Полуширина по Y строба по ошибкам
		int16_t DH       = 0; // Полуширина по H строба по ошибкам
		uint16_t Daz_big = 0; // Полуширина по азимуту строба по маневру
		uint16_t DD_big  = 0; // Полуширина по дальности строба по маневру
		int16_t Dum_big  = 0; // Полуширина по углу места строба по маневру
		int16_t DX_big   = 0; // Полуширина по X строба по маневру
		int16_t DY_big   = 0; // Полуширина по Y строба по маневру
		int16_t DH_big   = 0; // Полуширина по H строба по маневру

		void setDegreesAz(double v) { az = v / degLSB; }
		double getDegreesAz() const { return az * degLSB; }
		void setDegreesUm(double v) { um = v / 0.1; }
		double getDegreesUm() const { return um * 0.1; }
		void setDegreesDAz(double v) { Daz = v / degLSB; }
		double getDegreesDAz() const { return Daz * degLSB; }
		void setDegreesDUm(double v) { Dum = v / 0.1; }
		double getDegreesDUm() const { return Dum * 0.1; }
		void setDegreesDAzBig(double v) { Daz_big = v / degLSB; }
		double getDegreesDAzBig() const { return Daz_big * degLSB; }
		void setDegreesDUmBig(double v) { Dum_big = v / 0.1; }
		double getDegreesDUmBig() const { return Dum_big * 0.1; }
	};


	struct KORTR: public Header PIMETA(dest=voi, port=5301, no-stream) {
		static constexpr int16_t Port = 5301;
		static constexpr int16_t Type = 6;
		static constexpr int16_t Code = -1;
		KORTR(): Header(Type, 0) {}
		// msg_type:
		// 1 - Ввод
		// 2 - 1ВВ
		// 3 - 2ВВ
		// 4 - Сброс
		uint16_t az = 0; // Азимут цели
		uint16_t D  = 0; // Дальность цели, 1 м
		uint16_t N  = 0; // Номер трассы
		void setDegreesAz(double v) { az = v / degLSB; }
		double getDegreesAz() const { return az * degLSB; }
	};


	struct KV_KORTR: public Header PIMETA(dest=soi, port=5300, no-stream) {
		static constexpr int16_t Port = 5300;
		static constexpr int16_t Type = 6;
		static constexpr int16_t Code = -1;
		KV_KORTR(): Header(Type, 0) {}
		// msg_type:
		// 0 - не выполнено
		// 1 - выполнено
		uint16_t az = 0; // Азимут цели
		uint16_t D  = 0; // Дальность цели, 1 м
		PIString message;
		// char[]
		void setDegreesAz(double v) { az = v / degLSB; }
		double getDegreesAz() const { return az * degLSB; }
	};


	struct Zone PIMETA(no-stream){
		uint16_t azn = 0; // Азимут начала зоны
		uint16_t Dn  = 0; // Дальность начала зоны
		uint16_t azk = 0; // Азимут конца зоны
		uint16_t Dk  = 0; // Дальность конца зоны
		void setDegreesAzN(double v) { azn = v / degLSB; }
		double getDegreesAzN() const { return azn * degLSB; }
		void setDegreesAzK(double v) { azk = v / degLSB; }
		double getDegreesAzK() const { return azk * degLSB; }
	};


	struct CMD_ZZT: public Header PIMETA(dest=voi, port=5301, no-stream) {
		static constexpr int16_t Port = 5301;
		static constexpr int16_t Type = 7;
		static constexpr int16_t Code = -1;
		CMD_ZZT(): Header(Type, 0) {}
		// msg_type:
		// 0 - запрос зон
		// 1 - добавить зону
		// 2 - удалить зону
		Zone zone;
	};


	struct ZZT: public Header PIMETA(dest=soi, port=5300, no-stream) {
		static constexpr int16_t Port = 5300;
		static constexpr int16_t Type = 7;
		static constexpr int16_t Code = -1;
		ZZT(): Header(Type, 0) {}
		PIVector<Zone> zones;
		// msg_type: количество зон
		// ...
		// Zone[msg_code]
	};


	struct CMD_ZBL: public Header PIMETA(dest=voi, port=5301, no-stream) {
		static constexpr int16_t Port = 5301;
		static constexpr int16_t Type = 8;
		static constexpr int16_t Code = -1;
		CMD_ZBL(): Header(Type, 0) {}
		// msg_type:
		// 0 - запрос зон
		// 1 - добавить зону
		// 2 - удалить зону
		Zone zone;
	};


	struct ZBL: public Header PIMETA(dest=soi, port=5300, no-stream) {
		static constexpr int16_t Port = 5300;
		static constexpr int16_t Type = 8;
		static constexpr int16_t Code = -1;
		ZBL(): Header(Type, 0) {}
		PIVector<Zone> zones;
		// msg_type: количество зон
		// ...
		// Zone[msg_code]
	};


	struct AZIMUTH: public Header PIMETA(dest=all, port=5300, no-stream) {
		static constexpr int16_t Port = 5300;
		static constexpr int16_t Type = 9;
		static constexpr int16_t Code = -1;
		AZIMUTH(): Header(Type, 0) {}
		// msg_type: Признак перезапуска ПО ВОИ
		uint32_t T_voi  = 0; // Время ЭВМ ВОИ
		uint32_t T_poi  = 0; // Время ПОИ
		uint16_t az_poi = 0; // Азимут выхода системы ПОИ
		uint16_t az_ant = 0; // Азимут антенны (незадержанный в обработке)
		void setDegreesAzPOI(double v) { az_poi = v / degLSB; }
		double getDegreesAzPOI() const { return az_poi * degLSB; }
		void setDegreesAzANT(double v) { az_ant = v / degLSB; }
		double getDegreesAzANT() const { return az_ant * degLSB; }
		void setSecondsVOI(double s) { T_voi = s / timeLSB; }
		double getSecondsVOI() const { return T_voi * timeLSB; }
		void setSecondsPOI(double s) { T_poi = s / timeLSB; }
		double getSecondsPOI() const { return T_poi * timeLSB; }
	};


	/// ???

	struct VKL_REG: public Header PIMETA(dest=reg, port=6100, no-stream) {
		static constexpr int16_t Port = 6100;
		static constexpr int16_t Type = 5;
		static constexpr int16_t Code = 0;
		VKL_REG(): Header(Type, Code) {}
	};

	struct OTKL_REG: public Header PIMETA(dest=reg, port=6100, no-stream) {
		static constexpr int16_t Port = 6100;
		static constexpr int16_t Type = 8;
		static constexpr int16_t Code = 0;
		OTKL_REG(): Header(Type, Code) {}
	};

	struct ZPR_SOST_REG: public Header PIMETA(dest=reg, port=6100, no-stream) {
		static constexpr int16_t Port = 6100;
		static constexpr int16_t Type = 9;
		static constexpr int16_t Code = 0;
		ZPR_SOST_REG(): Header(Type, Code) {}
	};

	struct SOST_REG: public Header PIMETA(dest=soi, port=6101, no-stream) {
		static constexpr int16_t Port = 6101;
		static constexpr int16_t Type = 9;
		static constexpr int16_t Code = 0;
		SOST_REG(): Header(Type, Code) {}
		uint32_t disk_size = 0; //
		uint32_t disk_free = 0; //
		uint8_t mon        = 0;
		uint8_t year       = 0;
		uint8_t free_perc  = 0;
		uint8_t day        = 0;
		uint8_t min        = 0;
		uint8_t hour       = 0;
		uint8_t dhour      = 0;
		uint8_t sec        = 0;
		uint8_t dsec       = 0;
		uint8_t dmin       = 0;
	};

#pragma pack(pop)

private:
	static constexpr float degLSB       = 0.0054931640625;
	static constexpr double timeLSB     = 0.001;
	static constexpr float levelApukLSB = 0.5;
};


BINARY_STREAM_READ(Protocol_RLS_Mini::POI_TK_Kvit) {
	s >> PIMemoryBlock(&v, sizeof(v) - sizeof(v.words));
	v.words.resize(piMini(v.nw, s.binaryStreamSize() / 2));
	s >> PIMemoryBlock(v.words.data(), v.words.size() * 2);
	return s;
}

BINARY_STREAM_WRITE(Protocol_RLS_Mini::POI_TK_Kvit) {
	const_cast<Protocol_RLS_Mini::POI_TK_Kvit &>(v).nw = v.words.size();
	s << PIMemoryBlock(&v, sizeof(v) - sizeof(v.words));
	s << PIMemoryBlock(v.words.data(), v.words.size() * 2);
	return s;
}


BINARY_STREAM_READ(Protocol_RLS_Mini::TRVO) {
	s >> PIMemoryBlock(&v, sizeof(v) - sizeof(v.KTA));
	v.KTA.resize(piMini(v.msg_code, s.binaryStreamSize() / 2));
	s >> PIMemoryBlock(v.KTA.data(), v.KTA.size() * 2);
	return s;
}

BINARY_STREAM_WRITE(Protocol_RLS_Mini::TRVO) {
	const_cast<Protocol_RLS_Mini::TRVO &>(v).msg_code = v.KTA.size();
	s << PIMemoryBlock(&v, sizeof(v) - sizeof(v.KTA));
	s << PIMemoryBlock(v.KTA.data(), v.KTA.size() * 2);
	return s;
}


BINARY_STREAM_READ(Protocol_RLS_Mini::TRETA) {
	s >> PIMemoryBlock(&v, sizeof(v) - sizeof(v.KTA));
	return s;
}

BINARY_STREAM_WRITE(Protocol_RLS_Mini::TRETA) {
	s << PIMemoryBlock(&v, sizeof(v) - sizeof(v.KTA));
	return s;
}


BINARY_STREAM_READ(Protocol_RLS_Mini::KV_KORTR) {
	s >> PIMemoryBlock(&v, sizeof(v) - sizeof(v.message));
	int chars = s.binaryStreamSize() - 1;
	if (chars > 0) {
		PIByteArray char_data(chars);
		s >> PIMemoryBlock(char_data.data(), char_data.size());
		v.message = PIString::fromAscii(char_data);
	} else
		v.message.clear();
	return s;
}

BINARY_STREAM_WRITE(Protocol_RLS_Mini::KV_KORTR) {
	s << PIMemoryBlock(&v, sizeof(v) - sizeof(v.message));
	s << v.message.toAscii() << '\0';
	return s;
}


BINARY_STREAM_READ(Protocol_RLS_Mini::ZZT) {
	s >> PIMemoryBlock(&v, sizeof(v) - sizeof(v.zones));
	v.zones.resize(piMini(v.msg_code, s.binaryStreamSize() / sizeof(Protocol_RLS_Mini::Zone)));
	s >> PIMemoryBlock(v.zones.data(), v.zones.size() * sizeof(Protocol_RLS_Mini::Zone));
	return s;
}

BINARY_STREAM_WRITE(Protocol_RLS_Mini::ZZT) {
	const_cast<Protocol_RLS_Mini::ZZT &>(v).msg_code = v.zones.size();
	s << PIMemoryBlock(&v, sizeof(v) - sizeof(v.zones));
	s << PIMemoryBlock(v.zones.data(), v.zones.size() * sizeof(Protocol_RLS_Mini::Zone));
	return s;
}


BINARY_STREAM_READ(Protocol_RLS_Mini::ZBL) {
	s >> PIMemoryBlock(&v, sizeof(v) - sizeof(v.zones));
	v.zones.resize(piMini(v.msg_code, s.binaryStreamSize() / sizeof(Protocol_RLS_Mini::Zone)));
	s >> PIMemoryBlock(v.zones.data(), v.zones.size() * sizeof(Protocol_RLS_Mini::Zone));
	return s;
}

BINARY_STREAM_WRITE(Protocol_RLS_Mini::ZBL) {
	const_cast<Protocol_RLS_Mini::ZBL &>(v).msg_code = v.zones.size();
	s << PIMemoryBlock(&v, sizeof(v) - sizeof(v.zones));
	s << PIMemoryBlock(v.zones.data(), v.zones.size() * sizeof(Protocol_RLS_Mini::Zone));
	return s;
}

inline PICout operator<<(PICout out, const Protocol_RLS_Mini::Header & v) {
	return out << "type:" << v.msg_type << "code:" << v.msg_code;
}

inline PICout operator<<(PICout out, const Protocol_RLS_Mini::RR_Zapros & v) {
	return out;
}

inline PICout operator<<(PICout out, const Protocol_RLS_Mini::RR_Vr & v) {
	return out << "par:" << v.par;
}

inline PICout operator<<(PICout out, const Protocol_RLS_Mini::RR_Izl & v) {
	return out << "par:" << v.par;
}

inline PICout operator<<(PICout out, const Protocol_RLS_Mini::RR_Ant & v) {
	return out << "par:" << v.par;
}

inline PICout operator<<(PICout out, const Protocol_RLS_Mini::RR_TTek & v) {
	return out << "time:" << PICoutManipulators::Hex << v.time_msw << PICoutManipulators::Hex << v.time_lsw;
}

inline PICout operator<<(PICout out, const Protocol_RLS_Mini::RR_AzPopr & v) {
	return out << "daz:" << v.daz;
}

inline PICout operator<<(PICout out, const Protocol_RLS_Mini::RR_DPopr_POI & v) {
	return out << "dd:" << v.dd;
}

inline PICout operator<<(PICout out, const Protocol_RLS_Mini::RR_AzPopr_POI & v) {
	return out << "daz:" << v.daz;
}

// RR_Kvit
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::RR_Kvit & v) {
	return out << "ant:" << int(v.ant) << "izl:" << int(v.izl) << "kuizl:" << int(v.kuizl) << "vr:" << int(v.vr) << "kuvr:" << int(v.kuvr)
	           << "ispr_kan:" << PICoutManipulators::Bin << v.ispr_kan << "daz:" << v.daz << "dd_poi:" << v.dd_poi
	           << "daz_poi:" << v.daz_poi;
}

// POI_Zapros
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::POI_Zapros & v) {
	return out;
}

// POI_Shtat
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::POI_Shtat & v) {
	return out;
}

// POI_SDC
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::POI_SDC & v) {
	return out << "par:" << v.par;
}

// POI_DSA
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::POI_DSA & v) {
	return out << "par:" << v.par << "vr_n:" << v.vr_n << "vr_k:" << v.vr_k;
}

// POI_APU
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::POI_APU & v) {
	return out << "k1:" << v.k1 << "k2:" << v.k2;
}

// POI_Zona
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::POI_Zona & v) {
	return out << "k1:" << int(v.k1) << "k2:" << v.k2;
}

// POI_Kan
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::POI_Kan & v) {
	return out << "kan:" << PICoutManipulators::Bin << v.kan;
}

// POI_Kvit
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::POI_Kvit & v) {
	return out << "sdc:" << int(v.sdc) << "dsa:" << int(v.dsa) << "kan:" << PICoutManipulators::Bin << v.kan << "dsa_vr_n:" << v.dsa_vr_n
	           << "dsa_vr_k:" << v.dsa_vr_k << "apu_k1:" << v.apu_k1 << "apu_k2:" << v.apu_k2 << "zona_k1:" << int(v.zona_k1)
	           << "zona_k2:" << v.zona_k2;
}

// POI_TK_Zapros
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::POI_TK_Zapros & v) {
	return out << "kt:" << v.kt << "nkan:" << v.nkan << "reg_takt:" << v.reg_takt;
}

// POI_TK_Kvit
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::POI_TK_Kvit & v) {
	return out << "nw:" << v.nw << "words:";
}

// CIT_Zapros
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::CIT_Zapros & v) {
	return out;
}

// ImitatorTarget
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::ImitatorTarget & v) {
	return out << "on:" << int(v.on) << "reg:" << int(v.reg) << "az:" << v.az << "D:" << v.D << "um:" << v.um << "vr:" << v.vr
	           << "osl:" << v.osl;
}

// CIT_KU
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::CIT_KU & v) {
	return out << "it1:{" << v.it1 << "}"
	           << "it2:{" << v.it2 << "}";
}

// CIT_Kvit
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::CIT_Kvit & v) {
	return out << "it1:{" << v.it1 << "}"
	           << "it2:{" << v.it2 << "}";
}

// PI
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::PI & v) {
	return out << "aztek:" << v.aztek << "time:" << PICoutManipulators::Hex << v.time_msw << PICoutManipulators::Hex << v.time_lsw
	           << "az:" << v.az << "zona:" << v.zona << "nes:" << v.nes;
}

// KTA_VO
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::KTA_VO & v) {
	return out << "N:" << v.N << "T:" << v.T << "az:" << v.az << "um:" << v.um << "D:" << v.D << "porog:" << v.porog << "sp:" << v.sp
	           << "amp:" << v.amp << "vr:" << v.vr << "tn:" << int(v.tn) << "dum:" << int(v.dum);
}

// TRVO
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::TRVO & v) {
	out << "N:" << v.N << "ID:" << v.ID << "T:" << v.T << "az:" << v.az << "D:" << v.D << "um:" << v.um << "X:" << v.X << "Y:" << v.Y
		<< "H:" << v.H << "VX:" << v.VX << "VY:" << v.VY << "VH:" << v.VH << "VK:" << v.VK << "K:" << v.K << "mh:" << int(v.mh)
		<< "mk:" << int(v.mk) << "pv:" << int(v.pv) << "tn:" << int(v.tn) << "dum:" << int(v.dum) << "klass:" << int(v.klass)
		<< "sopr:" << int(v.sopr) << "KTA:";
	for (const auto & kta: v.KTA)
		out << kta;
	return out;
}

// TRETA
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::TRETA & v) {
	return out << static_cast<const Protocol_RLS_Mini::TRVO &>(v);
}

// STRSOPR
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::STRSOPR & v) {
	return out << "N:" << v.N << "az:" << v.az << "D:" << v.D << "um:" << v.um << "X:" << v.X << "Y:" << v.Y << "H:" << v.H
	           << "Daz:" << v.Daz << "DD:" << v.DD << "Dum:" << v.Dum << "DX:" << v.DX << "DY:" << v.DY << "DH:" << v.DH
	           << "Daz_big:" << v.Daz_big << "DD_big:" << v.DD_big << "Dum_big:" << v.Dum_big << "DX_big:" << v.DX_big
	           << "DY_big:" << v.DY_big << "DH_big:" << v.DH_big;
}

// KORTR
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::KORTR & v) {
	return out << "az:" << v.az << "D:" << v.D << "N:" << v.N;
}

// KV_KORTR
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::KV_KORTR & v) {
	return out << "az:" << v.az << "D:" << v.D << "message:" << v.message;
}

// Zone
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::Zone & v) {
	return out << "azn:" << v.azn << "Dn:" << v.Dn << "azk:" << v.azk << "Dk:" << v.Dk;
}

// CMD_ZZT
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::CMD_ZZT & v) {
	return out << "zone:{" << v.zone << "}";
}

// ZZT
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::ZZT & v) {
	out << "zones:";
	for (const auto & zone: v.zones)
		out << "{" << zone << "}";
	return out;
}

// CMD_ZBL
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::CMD_ZBL & v) {
	return out << "zone:{" << v.zone << "}";
}

// ZBL
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::ZBL & v) {
	out << "zones:";
	for (const auto & zone: v.zones)
		out << "{" << zone << "}";
	return out;
}

// AZIMUTH
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::AZIMUTH & v) {
	return out << "T_voi:" << v.T_voi << "T_poi:" << v.T_poi << "az_poi:" << v.az_poi << "az_ant:" << v.az_ant;
}

// VKL_REG
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::VKL_REG & v) {
	return out;
}

// OTKL_REG
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::OTKL_REG & v) {
	return out;
}

// ZPR_SOST_REG
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::ZPR_SOST_REG & v) {
	return out;
}

// SOST_REG
inline PICout operator<<(PICout out, const Protocol_RLS_Mini::SOST_REG & v) {
	return out << "disk_size:" << v.disk_size << "disk_free:" << v.disk_free << "mon:" << int(v.mon) << "year:" << int(v.year)
	           << "free_perc:" << int(v.free_perc) << "day:" << int(v.day) << "min:" << int(v.min) << "hour:" << int(v.hour)
	           << "dhour:" << int(v.dhour) << "sec:" << int(v.sec) << "dsec:" << int(v.dsec) << "dmin:" << int(v.dmin);
}

#endif // protocol_rls_mini_H
