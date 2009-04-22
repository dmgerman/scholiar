// #define RECOGNIZER_DEBUG  // uncomment for debug output

#define MAX_POLYGON_SIDES 4

#define LINE_MAX_DET 0.015   // maximum score for line (ideal line = 0)
#define CIRCLE_MIN_DET 0.95 // minimum det. score for circle (ideal circle = 1)
#define CIRCLE_MAX_SCORE 0.10 // max circle score for circle (ideal circle = 0)

#define SLANT_TOLERANCE (5*M_PI/180) // ignore slanting by +/- 5 degrees
#define RECTANGLE_ANGLE_TOLERANCE (15*M_PI/180) // angle tolerance in rectangles
#define RECTANGLE_LINEAR_TOLERANCE 0.20 // vertex gap tolerance in rectangles
#define POLYGON_LINEAR_TOLERANCE 0.20 // vertex gap tolerance in closed polygons

#define ARROW_MAXSIZE 0.8 // max size of arrow tip relative to main segment
#define ARROW_ANGLE_MIN (5*M_PI/180) // arrow tip angles relative to main segment
#define ARROW_ANGLE_MAX (50*M_PI/180)
#define ARROW_ASYMMETRY_MAX_ANGLE (30*M_PI/180)
#define ARROW_ASYMMETRY_MAX_LINEAR 1.0 // size imbalance of two legs of tip
#define ARROW_TIP_LINEAR_TOLERANCE 0.30 // gap tolerance on tip segments
#define ARROW_SIDEWAYS_GAP_TOLERANCE 0.25 // gap tolerance in lateral direction
#define ARROW_MAIN_LINEAR_GAP_MIN -0.3 // gap tolerance on main segment
#define ARROW_MAIN_LINEAR_GAP_MAX +0.7 // gap tolerance on main segment

void recognize_patterns(void);
void reset_recognizer(void);
