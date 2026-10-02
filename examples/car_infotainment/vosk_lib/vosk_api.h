#ifndef VOSK_API_H
#define VOSK_API_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct  __attribute__((packed, aligned(1))) VoskModel VoskModel;

typedef struct  __attribute__((packed, aligned(1))) VoskSpkModel VoskSpkModel;

typedef struct  __attribute__((packed, aligned(1))) VoskRecognizer VoskRecognizer;

typedef struct  __attribute__((packed, aligned(1))) VoskTextProcessor VoskTextProcessor;

typedef struct  __attribute__((packed, aligned(1))) VoskBatchModel VoskBatchModel;

typedef struct  __attribute__((packed, aligned(1))) VoskBatchRecognizer VoskBatchRecognizer;

VoskModel *vosk_model_new(const char *model_path);

void vosk_model_free(VoskModel *model);

int vosk_model_find_word(VoskModel *model, const char *word);

VoskSpkModel *vosk_spk_model_new(const char *model_path);

void vosk_spk_model_free(VoskSpkModel *model);

VoskRecognizer *vosk_recognizer_new(VoskModel *model, float sample_rate);

VoskRecognizer *vosk_recognizer_new_spk(VoskModel *model, float sample_rate, VoskSpkModel *spk_model);

VoskRecognizer *vosk_recognizer_new_grm(VoskModel *model, float sample_rate, const char *grammar);

void vosk_recognizer_set_spk_model(VoskRecognizer *recognizer, VoskSpkModel *spk_model);

void vosk_recognizer_set_grm(VoskRecognizer *recognizer, char const *grammar);

void vosk_recognizer_set_max_alternatives(VoskRecognizer *recognizer, int max_alternatives);

void vosk_recognizer_set_words(VoskRecognizer *recognizer, int words);

void vosk_recognizer_set_partial_words(VoskRecognizer *recognizer, int partial_words);

void vosk_recognizer_set_nlsml(VoskRecognizer *recognizer, int nlsml);

typedef enum VoskEpMode {
    VOSK_EP_ANSWER_DEFAULT = 0,
    VOSK_EP_ANSWER_SHORT = 1,
    VOSK_EP_ANSWER_LONG = 2,
    VOSK_EP_ANSWER_VERY_LONG = 3,
} VoskEndpointerMode;

void vosk_recognizer_set_endpointer_mode(VoskRecognizer *recognizer,  VoskEndpointerMode mode);

void vosk_recognizer_set_endpointer_delays(VoskRecognizer *recognizer, float t_start_max, float t_end, float t_max);

int vosk_recognizer_accept_waveform(VoskRecognizer *recognizer, const char *data, int length);

int vosk_recognizer_accept_waveform_s(VoskRecognizer *recognizer, const short *data, int length);

int vosk_recognizer_accept_waveform_f(VoskRecognizer *recognizer, const float *data, int length);

const char *vosk_recognizer_result(VoskRecognizer *recognizer);

const char *vosk_recognizer_partial_result(VoskRecognizer *recognizer);

const char *vosk_recognizer_final_result(VoskRecognizer *recognizer);

void vosk_recognizer_reset(VoskRecognizer *recognizer);

void vosk_recognizer_free(VoskRecognizer *recognizer);

void vosk_set_log_level(int log_level);

void vosk_gpu_init();

void vosk_gpu_thread_init();

VoskBatchModel *vosk_batch_model_new(const char *model_path);

void vosk_batch_model_free(VoskBatchModel *model);

void vosk_batch_model_wait(VoskBatchModel *model);

VoskBatchRecognizer *vosk_batch_recognizer_new(VoskBatchModel *model, float sample_rate);

void vosk_batch_recognizer_free(VoskBatchRecognizer *recognizer);

void vosk_batch_recognizer_accept_waveform(VoskBatchRecognizer *recognizer, const char *data, int length);

void vosk_batch_recognizer_set_nlsml(VoskBatchRecognizer *recognizer, int nlsml);

void vosk_batch_recognizer_finish_stream(VoskBatchRecognizer *recognizer);

const char *vosk_batch_recognizer_front_result(VoskBatchRecognizer *recognizer);

void vosk_batch_recognizer_pop(VoskBatchRecognizer *recognizer);

int vosk_batch_recognizer_get_pending_chunks(VoskBatchRecognizer *recognizer);

VoskTextProcessor *vosk_text_processor_new(const char *tagger, const char *verbalizer);

void vosk_text_processor_free(VoskTextProcessor *processor);

char *vosk_text_processor_itn(VoskTextProcessor *processor, const char *input);

#ifdef __cplusplus
}
#endif

#endif
