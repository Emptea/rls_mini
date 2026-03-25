#ifndef protocol_rls_mini_H
#define protocol_rls_mini_H

#include <piethernet.h>

class Protocol_RLS_Mini {
public:
#pragma pack(push, 1)

	enum control_point {
		CTRL = 0x00,
		ADC  = 0x01,
		PHD  = 0x02,
		PLL  = 0x03,
		OPH  = 0x04,
		LOU  = 0x05,
		KN   = 0x06,
		AD   = 0x07,
		APU  = 0x08,
	};

	struct Header PIMETA(no-stream) {
		Header(uint8_t t = 0, uint8_t c = 0) {
			msg_type = t;
			msg_code = c;
		}
		uint8_t msg_type; // Тип сообщения
		uint8_t msg_code; // Код операции
	};


	// RR

	struct RR_Zapros: public Header PIMETA(dest=upr, port=5001, no-stream) {
		static const int16_t Port = 5001;
		static const int16_t Type = 1;
		static const int16_t Code = 0;
		RR_Zapros(): Header(Type, Code) {}
	};


	struct RR_Vr: public Header PIMETA(dest=upr, port=5001, no-stream) {
		static const int16_t Port = 5001;
		static const int16_t Type = 1;
		static const int16_t Code = 1;
		RR_Vr(): Header(Type, Code) {}
		uint16_t par = 0; // 0 - выключить вращение и имитатор вращения, 1 - включить вращение, 2 - включить имитатор вращения
	};


	struct RR_Izl: public Header PIMETA(dest=upr, port=5001, no-stream) {
		static const int16_t Port = 5001;
		static const int16_t Type = 1;
		static const int16_t Code = 2;
		RR_Izl(): Header(Type, Code) {}
		uint16_t par = 0; // 0 - выключить излучение, 1 - включить излучение
	};


	struct RR_Ant: public Header PIMETA(dest=upr, port=5001, no-stream) {
		static const int16_t Port = 5001;
		static const int16_t Type = 1;
		static const int16_t Code = 3;
		RR_Ant(): Header(Type, Code) {}
		uint16_t par = 0; // 0 - антенна отключена, 1 - антенна подключена
	};


	struct RR_TTek: public Header PIMETA(dest=upr, port=5001, no-stream) {
		static const int16_t Port = 5001;
		static const int16_t Type = 1;
		static const int16_t Code = 4;
		RR_TTek(): Header(Type, Code) {}
		uint32_t time = 0;
		void setSeconds(double s) { time = s / timeLSB; }
		double getSeconds() const { return time * timeLSB; }
	};


	struct RR_AzPopr: public Header PIMETA(dest=upr, port=5001, no-stream) {
		static const int16_t Port = 5001;
		static const int16_t Type = 1;
		static const int16_t Code = 5;
		RR_AzPopr(): Header(Type, Code) {}
		int16_t daz = 0;
		void setDegrees(double v) { daz = v / degLSB; }
		double getDegrees() const { return daz * degLSB; }
	};


	struct RR_DPopr_POI: public Header PIMETA(dest=upr, port=5001, no-stream) {
		static const int16_t Port = 5001;
		static const int16_t Type = 1;
		static const int16_t Code = 6;
		RR_DPopr_POI(): Header(Type, Code) {}
		int16_t dd = 0; // 1 м
	};


	struct RR_AzPopr_POI: public Header PIMETA(dest=upr, port=5001, no-stream) {
		static const int16_t Port = 5001;
		static const int16_t Type = 1;
		static const int16_t Code = 7;
		RR_AzPopr_POI(): Header(Type, Code) {}
		int16_t daz = 0;
		void setDegrees(double v) { daz = v / degLSB; }
		double getDegrees() const { return daz * degLSB; }
	};


	struct RR_Kvit: public Header PIMETA(dest=all, port=5000, no-stream) {
		static const int16_t Port = 5000;
		static const int16_t Type = 1;
		static const int16_t Code = 0;
		RR_Kvit(): Header(Type, Code) {}
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
		uint8_t ispr_kan = 0;
		int16_t daz      = 0;
		int16_t dd_poi   = 0; // 1 м
		int16_t daz_poi  = 0;
		void setDegreesDaz(double v) { daz = v / degLSB; }
		double getDegreesDaz() const { return daz * degLSB; }
		void setDegreesDazPOI(double v) { daz_poi = v / degLSB; }
		double getDegreesDazPOI() const { return daz_poi * degLSB; }
	};


	// POI

	struct POI_Zapros: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static const int16_t Port = 5101;
		static const int16_t Type = 2;
		static const int16_t Code = 0;
		POI_Zapros(): Header(Type, Code) {}
	};


	struct POI_Shtat: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static const int16_t Port = 5101;
		static const int16_t Type = 2;
		static const int16_t Code = 255;
		POI_Shtat(): Header(Type, Code) {}
	};


	struct POI_SDC: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static const int16_t Port = 5101;
		static const int16_t Type = 2;
		static const int16_t Code = 1;
		POI_SDC(): Header(Type, Code) {}
		uint16_t par = 0; // 0 - выключить СДЦ, 1 - включить СДЦ
	};


	struct POI_DSA: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static const int16_t Port = 5101;
		static const int16_t Type = 2;
		static const int16_t Code = 2;
		POI_DSA(): Header(Type, Code) {}
		uint16_t par  = 0; // 0 - выключить ДСА, 1 - включить ДСА
		uint16_t vr_n = 0; // Vr начала полосы режекции, 1 м/с
		uint16_t vr_k = 0; // Vr конца  полосы режекции, 1 м/с
	};


	struct POI_APU: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static const int16_t Port = 5101;
		static const int16_t Type = 2;
		static const int16_t Code = 3;
		POI_APU(): Header(Type, Code) {}
		uint16_t k1 = 0; // Коэффициент порога 1, цмр 0.5 дБ
		uint16_t k2 = 0; // Коэффициент порога 2, цмр 0.5 дБ
	};


	struct POI_Zona: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static const int16_t Port = 5101;
		static const int16_t Type = 2;
		static const int16_t Code = 4;
		POI_Zona(): Header(Type, Code) {}
		uint8_t k1 = 0; // Код строба расчета зоны
		uint8_t _reserve;
		uint16_t k2 = 0; // Код канала расчета зоны
	};


	struct POI_Kan: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static const int16_t Port = 5101;
		static const int16_t Type = 2;
		static const int16_t Code = 5;
		POI_Kan(): Header(Type, Code) {}
		uint8_t _reserve;
		uint8_t kan = 0; // Подключенные каналы
	};


	struct POI_Kvit: public Header PIMETA(dest=soi, port=5100, no-stream) {
		static const int16_t Port = 5100;
		static const int16_t Type = 2;
		static const int16_t Code = 0;
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
	};


	struct POI_TK_Zapros: public Header PIMETA(dest=poi, port=5101, no-stream) {
		static const int16_t Port = 5101;
		static const int16_t Type = 3;
		static const int16_t Code = 0;
		POI_TK_Zapros(): Header(Type, Code) {} // 0 - запрос состояния системы ПОИ, 1 - запрос контрольной точки
		uint16_t kt       = 0;                 // Код контрольной точки
		uint16_t nkan     = 0;                 // Номер канала
		uint16_t reg_takt = 0; // Количество регистрируемых тактов: 0 - запрос состояния системы ПОИ, 1 - запрос контрольной точки
	};


	struct POI_TK_Kvit: public Header PIMETA(dest=soi, port=5100, no-stream) {
		static const int16_t Port = 5100;
		static const int16_t Type = 3;
		static const int16_t Code = 0;
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
		static const int16_t Port = 5401;
		static const int16_t Type = 9;
		static const int16_t Code = 0;
		CIT_Zapros(): Header(Type, Code) {}
	};


	struct ImitatorTarget PIMETA(no-stream) {
		uint8_t on   = 0; // 0 - Выключена, 1 - Включен
		uint8_t reg  = 0; // 0 - POA, 1 - PBA
		uint16_t az  = 0; // Азимут
		uint16_t D   = 0; // Дальность, 1 м
		int16_t um   = 0; // Угол места
		uint16_t vr  = 0; // Радиальная скорость, 1 м/с
		uint16_t osl = 0; // Ослабление,  0.5 дБ
		void setDegreesAz(double v) { az = v / degLSB; }
		double getDegreesAz() const { return az * degLSB; }
		void setDegreesUm(double v) { um = v / 0.1; }
		double getDegreesUm() const { return um * 0.1; }
	};


	struct CIT_KU: public Header PIMETA(dest=it, port=5401, no-stream) {
		static const int16_t Port = 5401;
		static const int16_t Type = 9;
		static const int16_t Code = 1;
		CIT_KU(): Header(Type, Code) {}
		ImitatorTarget it1;
		ImitatorTarget it2;
	};


	struct CIT_Kvit: public Header PIMETA(dest=soi, port=5400, no-stream) {
		static const int16_t Port = 5400;
		static const int16_t Type = 9;
		static const int16_t Code = 0;
		CIT_Kvit(): Header(Type, Code) {}
		ImitatorTarget it1;
		ImitatorTarget it2;
	};


	// PI

	struct PI: public Header PIMETA(dest=all, port=5200, no-stream) {
		static const int16_t Port = 5200;
		static const int16_t Type = 4;
		static const int16_t Code = 0;
		PI(): Header(Type, Code) {}
		uint16_t aztek = 0; // Азимут текущий, незадержанный в ПОИ
		uint32_t T     = 0; // Время, задержанное в обработке
		uint16_t az    = 0; // Азимут, задержанный в обработке
		uint16_t zona  = 0; // Дальность отметки "Зона"
		uint16_t nes   = 0; // Количество эхосигналов
		                    // Дальность эхосигнала
		                    // ...
		                    // uint16_t[nes]
		void setDegreesAzTek(double v) { aztek = v / degLSB; }
		double getDegreesAzTek() const { return aztek * degLSB; }
		void setDegreesAz(double v) { az = v / degLSB; }
		double getDegreesAz() const { return az * degLSB; }
		void setSeconds(double s) { T = s / timeLSB; }
		double getSeconds() const { return T * timeLSB; }
	};


	struct KTA_VO: public Header PIMETA(dest=all, port=5202, no-stream) {
		static const int16_t Port = 5202;
		static const int16_t Type = 4;
		static const int16_t Code = 1;
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
		static const int16_t Port = 5300;
		static const int16_t Type = 5;
		static const int16_t Code = -1;
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
				uint8_t pv       : 1; // Признак выдачи координат и времени: 0 - на момент выдачи, 1 - на момент локации
				uint8_t tn       : 1; // Признак тренажной цели
				uint8_t dum      : 1; // Признак достоверности измерения угла места цели
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
		static const int16_t Port = 5300;
		static const int16_t Type = 10;
		static const int16_t Code = -1;
		TRETA() { msg_type = 10; }
	};


	struct STRSOPR: public Header PIMETA(dest=reg, port=5300, no-stream) {
		static const int16_t Port = 5300;
		static const int16_t Type = 11;
		static const int16_t Code = 0;
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
		static const int16_t Port = 5301;
		static const int16_t Type = 6;
		static const int16_t Code = -1;
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
		static const int16_t Port = 5300;
		static const int16_t Type = 6;
		static const int16_t Code = -1;
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
		static const int16_t Port = 5301;
		static const int16_t Type = 7;
		static const int16_t Code = -1;
		CMD_ZZT(): Header(Type, 0) {}
		// msg_type:
		// 0 - запрос зон
		// 1 - добавить зону
		// 2 - удалить зону
		Zone zone;
	};


	struct ZZT: public Header PIMETA(dest=soi, port=5300, no-stream) {
		static const int16_t Port = 5300;
		static const int16_t Type = 7;
		static const int16_t Code = -1;
		ZZT(): Header(Type, 0) {}
		PIVector<Zone> zones;
		// msg_type: количество зон
		// ...
		// Zone[msg_code]
	};


	struct CMD_ZBL: public Header PIMETA(dest=voi, port=5301, no-stream) {
		static const int16_t Port = 5301;
		static const int16_t Type = 8;
		static const int16_t Code = -1;
		CMD_ZBL(): Header(Type, 0) {}
		// msg_type:
		// 0 - запрос зон
		// 1 - добавить зону
		// 2 - удалить зону
		Zone zone;
	};


	struct ZBL: public Header PIMETA(dest=soi, port=5300, no-stream) {
		static const int16_t Port = 5300;
		static const int16_t Type = 8;
		static const int16_t Code = -1;
		ZBL(): Header(Type, 0) {}
		PIVector<Zone> zones;
		// msg_type: количество зон
		// ...
		// Zone[msg_code]
	};


	struct AZIMUTH: public Header PIMETA(dest=all, port=5300, no-stream) {
		static const int16_t Port = 5300;
		static const int16_t Type = 9;
		static const int16_t Code = -1;
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
		static const int16_t Port = 6100;
		static const int16_t Type = 5;
		static const int16_t Code = 0;
		VKL_REG(): Header(Type, Code) {}
	};

	struct OTKL_REG: public Header PIMETA(dest=reg, port=6100, no-stream) {
		static const int16_t Port = 6100;
		static const int16_t Type = 8;
		static const int16_t Code = 0;
		OTKL_REG(): Header(Type, Code) {}
	};

	struct ZPR_SOST_REG: public Header PIMETA(dest=reg, port=6100, no-stream) {
		static const int16_t Port = 6100;
		static const int16_t Type = 9;
		static const int16_t Code = 0;
		ZPR_SOST_REG(): Header(Type, Code) {}
	};

	struct SOST_REG: public Header PIMETA(dest=soi, port=6101, no-stream) {
		static const int16_t Port = 6101;
		static const int16_t Type = 9;
		static const int16_t Code = 0;
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
	static constexpr double degLSB  = 0.0054931640625;
	static constexpr double timeLSB = 0.001;
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

#endif // protocol_rls_mini_H
