/**
 * AirControlX - Automated Air Traffic Control System
 * Terminal-based implementation for Operating Systems Project Spring 2025
 */

 #include <iostream>
 #include <vector>
 #include <queue>
 #include <map>
 #include <string>
 #include <thread>
 #include <mutex>
 #include <condition_variable>
 #include <chrono>
 #include <random>
 #include <iomanip>
 #include <sstream>
 #include <memory>
 #include <algorithm>
 #include <ctime>
 #include <unistd.h> // For fork() and pipes
 #include <fcntl.h>
 #include <sys/wait.h>
 #include <sys/select.h>
 #include <cstring> // Added for strncpy
// Add this with the other includes if it's not there already (around line 15)
#include <set>

// Add these includes at the top of the file, after the existing includes
#include <termios.h>
#include <limits>
#include <sys/ioctl.h>
#include <signal.h>

 using namespace std;
 
 // -------- GLOBAL DEFINITIONS AND ENUMS --------
 
 // Flight types
 enum class FlightType { COMMERCIAL, CARGO, EMERGENCY };
 
 // Aircraft states for arrivals
 enum class ArrivalState { HOLDING, APPROACH, LANDING, TAXI, AT_GATE };
 
 // Aircraft states for departures
 enum class DepartureState { AT_GATE, TAXI, TAKEOFF_ROLL, CLIMB, CRUISE };
 
 // Direction of flight
 enum class Direction { NORTH, SOUTH, EAST, WEST };
 
 // Runway designations
 enum class Runway { RWY_A, RWY_B, RWY_C, NONE };
 
 // Payment status
 enum class PaymentStatus { UNPAID, PAID, OVERDUE };
 
 // Message types for IPC
 enum class MessageType {
     AVN_CREATED,
     PAYMENT_REQUEST,
     PAYMENT_CONFIRMATION,
     QUERY_AVN,
     QUERY_AIRLINE
 };
 
 // IPC Message structure with fixed-size strings
 struct IPCMessage {
     MessageType type;
     int avnId;
     char airline[32]; // Fixed-size buffer for airline name
     char flightNumber[16]; // Fixed-size buffer for flight number
     double amount;
     char details[64]; // Fixed-size buffer for details
     int minSpeed; // Added for speed range
     int maxSpeed; // Added for speed range
     
     IPCMessage() : type(MessageType::AVN_CREATED), avnId(0), amount(0.0), minSpeed(0), maxSpeed(0) {
         airline[0] = '\0';
         flightNumber[0] = '\0';
         details[0] = '\0';
     }
 };
 
 // -------- SYSTEM CONFIGURATION --------
 
 // Simulation parameters
 const int SIMULATION_TIME = 300; // 5 minutes in seconds
 const int ARRIVAL_NORTH_INTERVAL = 180; // 3 minutes
 const int ARRIVAL_SOUTH_INTERVAL = 120; // 2 minutes
 const int DEPARTURE_EAST_INTERVAL = 150; // 2.5 minutes
 const int DEPARTURE_WEST_INTERVAL = 240; // 4 minutes
 
 // Emergency probabilities
 const int NORTH_EMERGENCY_PROBABILITY = 10; // 10%
 const int SOUTH_EMERGENCY_PROBABILITY = 5;  // 5%
 const int EAST_EMERGENCY_PROBABILITY = 15;  // 15%
 const int WEST_EMERGENCY_PROBABILITY = 20;  // 20%
 
 // Speed rules for arrivals
 const int HOLDING_MIN_SPEED = 400;
 const int HOLDING_MAX_SPEED = 600;
 const int APPROACH_MIN_SPEED = 240;
 const int APPROACH_MAX_SPEED = 290;
 const int LANDING_START_SPEED = 240;
 const int LANDING_END_SPEED = 30;
 const int TAXI_MIN_SPEED = 15;
 const int TAXI_MAX_SPEED = 30;
 const int GATE_MAX_SPEED = 5;
 
 // Speed rules for departures
 const int TAKEOFF_MAX_SPEED = 290;
 const int CLIMB_MIN_SPEED = 250;
 const int CLIMB_MAX_SPEED = 463;
 const int CRUISE_MIN_SPEED = 800;
 const int CRUISE_MAX_SPEED = 900; 
 
 // Fine amounts
 const int COMMERCIAL_FINE = 500000; // PKR
 const int CARGO_FINE = 700000; // PKR
 const float SERVICE_FEE_PERCENTAGE = 0.15; // 15%
 
 const int VIOLATION_PROBABILITY = 15; // 15% chance of a speed violation
 const int MAX_VIOLATION_SPEED_EXCESS = 40; // Max km/h over the limit
 
 // -------- SHARED RESOURCES --------
 
 // Mutex for console output
 mutex cout_mutex;
 
 // Mutexes for runway access
 mutex runway_a_mutex;
 mutex runway_b_mutex;
 mutex runway_c_mutex;
 
 // Mutex for AVN data
 mutex avn_mutex;
 
 // Random number generator
 random_device rd;
 mt19937 gen(rd());
 
 // -------- CLASS DEFINITIONS --------
 
 // Forward declarations
 class Aircraft;
 class FlightScheduler;
 
 // Airspace Violation Notice (AVN)
 class AVN {
 public:
     int id;
     string airline;
     string flightNumber;
     FlightType aircraftType;
     int recordedSpeed;
     int permissibleSpeedMin;
     int permissibleSpeedMax;
     time_t issueTime;
     time_t dueDate;
     double fineAmount;
     double serviceFee;
     double totalAmount;
     PaymentStatus status;
 
     AVN(int id, const string& airline, const string& flightNumber, FlightType type, 
         int recordedSpeed, int permissibleSpeedMin, int permissibleSpeedMax) 
         : id(id), airline(airline), flightNumber(flightNumber), aircraftType(type),
           recordedSpeed(recordedSpeed), permissibleSpeedMin(permissibleSpeedMin), 
           permissibleSpeedMax(permissibleSpeedMax), status(PaymentStatus::UNPAID) {
         
         // Set issue time to current time
         issueTime = time(nullptr);
         
         // Set due date to 3 days from now
         dueDate = issueTime + (3 * 24 * 60 * 60);
         
         // Calculate fine amount based on aircraft type
         if (type == FlightType::COMMERCIAL) {
             fineAmount = COMMERCIAL_FINE;
         } else {
             fineAmount = CARGO_FINE;
         }
         
         // Calculate service fee
         serviceFee = fineAmount * SERVICE_FEE_PERCENTAGE;
         
         // Calculate total amount
         totalAmount = fineAmount + serviceFee;
     }
 
     string getStatusString() const {
         switch (status) {
             case PaymentStatus::UNPAID: return "Unpaid";
             case PaymentStatus::PAID: return "Paid";
             case PaymentStatus::OVERDUE: return "Overdue";
             default: return "Unknown";
         }
     }
 
     string getAircraftTypeString() const {
         switch (aircraftType) {
             case FlightType::COMMERCIAL: return "Commercial";
             case FlightType::CARGO: return "Cargo";
             case FlightType::EMERGENCY: return "Emergency";
             default: return "Unknown";
         }
     }
 
     string getFormattedTime(time_t timeValue) const {
         struct tm* timeinfo = localtime(&timeValue);
         char buffer[80];
         strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
         return string(buffer);
     }
 
     void printDetails() const {
         lock_guard<mutex> lock(cout_mutex);
         cout << "============= AVN DETAILS =============" << endl;
         cout << "AVN ID: " << id << endl;
         cout << "Airline: " << airline << endl;
         cout << "Flight Number: " << flightNumber << endl;
         cout << "Aircraft Type: " << getAircraftTypeString() << endl;
         cout << "Speed Recorded: " << recordedSpeed << " km/h" << endl;
         cout << "Permissible Speed Range: " << permissibleSpeedMin << "-" << permissibleSpeedMax << " km/h" << endl;
         cout << "Issue Date/Time: " << getFormattedTime(issueTime) << endl;
         cout << "Due Date: " << getFormattedTime(dueDate) << endl;
         cout << "Fine Amount: PKR " << fixed << setprecision(2) << fineAmount << endl;
         cout << "Service Fee: PKR " << fixed << setprecision(2) << serviceFee << endl;
         cout << "Total Due: PKR " << fixed << setprecision(2) << totalAmount << endl;
         cout << "Payment Status: " << getStatusString() << endl;
         cout << "=======================================" << endl;
     }
 };
 
 // Airline class
 class Airline {
 public:
     string name;
     int totalAircrafts;
     int activeFlights;
     vector<shared_ptr<AVN>> violations;
     
     Airline(const string& name, int totalAircrafts, int activeFlights)
         : name(name), totalAircrafts(totalAircrafts), activeFlights(activeFlights) {}
     
     void addViolation(shared_ptr<AVN> violation) {
         lock_guard<mutex> lock(avn_mutex);
         violations.push_back(violation);
     }
 
     void printViolations() const {
         lock_guard<mutex> lock(cout_mutex);
         cout << "==== Violations for " << name << " ====" << endl;
         
         if (violations.empty()) {
             cout << "No violations recorded." << endl;
         } else {
             for (const auto& avn : violations) {
                 cout << "AVN ID: " << avn->id << " | Flight: " << avn->flightNumber 
                      << " | Status: " << avn->getStatusString() 
                      << " | Amount: PKR " << fixed << setprecision(2) << avn->totalAmount << endl;
             }
         }
         cout << "================================" << endl;
     }
 };
 
 // Aircraft class (base for both arrival and departure)
 class Aircraft {
 protected:
     static int nextId;
     
 public:
     int id;
     string flightNumber;
     string airline;
     FlightType type;
     Direction direction;
     int priority;
     int currentSpeed;
     bool hasActiveViolation;
     shared_ptr<AVN> currentViolation;
     chrono::system_clock::time_point scheduledTime;
     chrono::system_clock::time_point actualTime;
     Runway assignedRunway;
     bool isEmergency;
     // Add to Aircraft base class (around line 240) after the other member variables:

// Track which states have already had violations
std::set<string> violatedStates;
// Add a new member variable to the Aircraft base class (around line 241)
bool maintainViolationSpeed = false;
int violationSpeed = 0;

     Aircraft(const string& flightNumber, const string& airline, FlightType type, 
              Direction direction, int priority, 
              chrono::system_clock::time_point scheduledTime)
         : id(nextId++), flightNumber(flightNumber), airline(airline), type(type),
           direction(direction), priority(priority), currentSpeed(0),
           hasActiveViolation(false), scheduledTime(scheduledTime),
           assignedRunway(Runway::NONE), isEmergency(false) {}
     
     virtual ~Aircraft() {}
     
     virtual void updateStatus(int simulationTime) = 0;
     virtual void checkViolation() = 0;
     virtual string getStateString() const = 0;
     virtual bool isCompleted() const = 0;
     
     string getRunwayString() const {
         switch (assignedRunway) {
             case Runway::RWY_A: return "RWY-A";
             case Runway::RWY_B: return "RWY-B";
             case Runway::RWY_C: return "RWY-C";
             case Runway::NONE: return "None";
             default: return "Unknown";
         }
     }
     
     string getDirectionString() const {
         switch (direction) {
             case Direction::NORTH: return "North";
             case Direction::SOUTH: return "South";
             case Direction::EAST: return "East";
             case Direction::WEST: return "West";
             default: return "Unknown";
         }
     }
     
     string getTypeString() const {
         switch (type) {
             case FlightType::COMMERCIAL: return "Commercial";
             case FlightType::CARGO: return "Cargo";
             case FlightType::EMERGENCY: return "Emergency";
             default: return "Unknown";
         }
     }
     
     virtual string getSummary() const {
         stringstream ss;
         ss << flightNumber << " | " << airline << " | " << getTypeString() 
            << " | " << getDirectionString() << " | " << getStateString()
            << " | Speed: " << currentSpeed << " km/h | Runway: " << getRunwayString();
         if (isEmergency) ss << " | EMERGENCY";
         if (hasActiveViolation) ss << " | VIOLATION";
         return ss.str();
     }
 };
 
 int Aircraft::nextId = 1000;
 
 // Arrival Flight class
 class ArrivalFlight : public Aircraft {
 private:
     ArrivalState state;
     int stateTime; // Time spent in current state
     
     // State transition times (in simulation seconds)
     const int HOLDING_TIME = 20;
     const int APPROACH_TIME = 15;
     const int LANDING_TIME = 10;
     const int TAXI_TIME = 15;
     
 public:
     ArrivalFlight(const string& flightNumber, const string& airline, FlightType type, 
                   Direction direction, int priority, 
                   chrono::system_clock::time_point scheduledTime)
         : Aircraft(flightNumber, airline, type, direction, priority, scheduledTime),
           state(ArrivalState::HOLDING), stateTime(0) {
         
         // Set initial speed based on state
         uniform_int_distribution<> holdingDist(HOLDING_MIN_SPEED, HOLDING_MAX_SPEED);
         currentSpeed = holdingDist(gen);
     }
     
     ArrivalState getState() const {
         return state;
     }
     
     string getStateString() const override {
         switch (state) {
             case ArrivalState::HOLDING: return "Holding";
             case ArrivalState::APPROACH: return "Approach";
             case ArrivalState::LANDING: return "Landing";
             case ArrivalState::TAXI: return "Taxi";
             case ArrivalState::AT_GATE: return "At Gate";
             default: return "Unknown";
         }
     }
     
     // For ArrivalFlight::updateStatus (around line 398)

void updateStatus(int simulationTime) override {
    stateTime++;
    
    // Store previous state to detect state transitions
    ArrivalState previousState = state;
    
    // Update speed and state based on current state and time spent in that state
    switch (state) {
        case ArrivalState::HOLDING:
            if (stateTime >= HOLDING_TIME && assignedRunway != Runway::NONE) {
                state = ArrivalState::APPROACH;
                stateTime = 0;
                // Reset violation settings when changing state
                maintainViolationSpeed = false;
                
                if (!maintainViolationSpeed) {
                    uniform_int_distribution<> approachDist(APPROACH_MIN_SPEED, APPROACH_MAX_SPEED);
                    currentSpeed = approachDist(gen);
                }
            }
            break;
            
        case ArrivalState::APPROACH:
            if (stateTime >= APPROACH_TIME) {
                state = ArrivalState::LANDING;
                stateTime = 0;
                // Reset violation settings when changing state
                maintainViolationSpeed = false;
                
                if (!maintainViolationSpeed) {
                    currentSpeed = LANDING_START_SPEED;
                }
            }
            break;
            
        case ArrivalState::LANDING:
            if (!maintainViolationSpeed) {
                // Gradually decrease speed during landing
                currentSpeed = max(LANDING_END_SPEED, LANDING_START_SPEED - 
                                  (LANDING_START_SPEED - LANDING_END_SPEED) * stateTime / LANDING_TIME);
            }
            
            if (stateTime >= LANDING_TIME) {
                state = ArrivalState::TAXI;
                stateTime = 0;
                // Reset violation settings when changing state
                maintainViolationSpeed = false;
                
                if (!maintainViolationSpeed) {
                    uniform_int_distribution<> taxiDist(TAXI_MIN_SPEED, TAXI_MAX_SPEED);
                    currentSpeed = taxiDist(gen);
                }
            }
            break;
            
        case ArrivalState::TAXI:
            if (stateTime >= TAXI_TIME) {
                state = ArrivalState::AT_GATE;
                stateTime = 0;
                // Reset violation settings when changing state
                maintainViolationSpeed = false;
                currentSpeed = 0;
            }
            break;
            
        case ArrivalState::AT_GATE:
            maintainViolationSpeed = false;
            currentSpeed = 0;
            break;
    }
    
    // If state has changed, clear violation speed
    if (previousState != state) {
        maintainViolationSpeed = false;
    }
    
    // Randomly introduce speed violations with a configurable probability
    if (!hasActiveViolation && !isEmergency && !maintainViolationSpeed) {  // Don't give violations to emergency flights
        uniform_int_distribution<> violationChanceDist(1, 100);
        
        // Only proceed with violation logic if the random check passes
        // Make this a lower probability to ensure fewer aircraft get violations
        if (violationChanceDist(gen) <= VIOLATION_PROBABILITY / 3) {
            uniform_int_distribution<> violationDist(1, 100);
            if (violationDist(gen) <= VIOLATION_PROBABILITY) {
                // Determine excess speed based on current state
                int excessSpeed = 0;
                uniform_int_distribution<> excessDist(5, MAX_VIOLATION_SPEED_EXCESS);
                
                switch (state) {
                    case ArrivalState::HOLDING:
                        excessSpeed = excessDist(gen);
                        currentSpeed = HOLDING_MAX_SPEED + excessSpeed;
                        maintainViolationSpeed = true;
                        violationSpeed = currentSpeed;
                        break;
                        
                    case ArrivalState::APPROACH:
                        excessSpeed = excessDist(gen);
                        currentSpeed = APPROACH_MAX_SPEED + excessSpeed;
                        maintainViolationSpeed = true;
                        violationSpeed = currentSpeed;
                        break;
                        
                    case ArrivalState::LANDING:
                        if (stateTime > LANDING_TIME / 2) {
                            excessSpeed = excessDist(gen);
                            // Higher speed than should be at this point in landing
                            currentSpeed += excessSpeed;
                            maintainViolationSpeed = true;
                            violationSpeed = currentSpeed;
                        }
                        break;
                        
                    case ArrivalState::TAXI:
                        excessSpeed = excessDist(gen) / 2; // Less excess for taxi speeds
                        currentSpeed = TAXI_MAX_SPEED + excessSpeed;
                        maintainViolationSpeed = true;
                        violationSpeed = currentSpeed;
                        break;
                        
                    default:
                        break;
                }
            }
        }
    } else if (maintainViolationSpeed) {
        // Maintain the violation speed until state changes
        currentSpeed = violationSpeed;
    }
    
    // Check for violations
    checkViolation();
}
     
     // Modify the checkViolation method in ArrivalFlight class (around line 634)

void checkViolation() override {
    // Get current state as a string
    string currentStateStr = getStateString();
    
    // Skip violation check if we already had a violation in this state
    if (violatedStates.find(currentStateStr) != violatedStates.end()) {
        return;
    }
    
    bool violation = false;
    int minSpeed = 0, maxSpeed = 0;
    
    switch (state) {
        case ArrivalState::HOLDING:
            if (currentSpeed > HOLDING_MAX_SPEED) {
                violation = true;
                minSpeed = HOLDING_MIN_SPEED;
                maxSpeed = HOLDING_MAX_SPEED;
            }
            break;
            
        case ArrivalState::APPROACH:
            if (currentSpeed < APPROACH_MIN_SPEED || currentSpeed > APPROACH_MAX_SPEED) {
                violation = true;
                minSpeed = APPROACH_MIN_SPEED;
                maxSpeed = APPROACH_MAX_SPEED;
            }
            break;
            
        case ArrivalState::LANDING:
            if (currentSpeed > LANDING_START_SPEED || 
                (stateTime >= LANDING_TIME && currentSpeed > LANDING_END_SPEED)) {
                violation = true;
                minSpeed = 0;
                maxSpeed = LANDING_START_SPEED;
            }
            break;
            
        case ArrivalState::TAXI:
            if (currentSpeed > TAXI_MAX_SPEED) {
                violation = true;
                minSpeed = TAXI_MIN_SPEED;
                maxSpeed = TAXI_MAX_SPEED;
            }
            break;
            
        case ArrivalState::AT_GATE:
            if (currentSpeed > GATE_MAX_SPEED) {
                violation = true;
                minSpeed = 0;
                maxSpeed = GATE_MAX_SPEED;
            }
            break;
    }
    
    // If there's a violation
    if (violation) {
        hasActiveViolation = true;
        
        // Create new AVN
        static int avnIdCounter = 1000;
        currentViolation = make_shared<AVN>(
            avnIdCounter++, airline, flightNumber, type,
            currentSpeed, minSpeed, maxSpeed
        );
        
        // Add this state to the set of states that have had violations
        violatedStates.insert(currentStateStr);
        
        lock_guard<mutex> lock(cout_mutex);
        cout << "\nVIOLATION DETECTED! Flight " << flightNumber 
             << " (" << airline << ") - Speed: " << currentSpeed 
             << " km/h in " << getStateString() << " state.\n";
    }
}
     
     bool isCompleted() const override {
         return state == ArrivalState::AT_GATE;
     }
 };
 
 // Departure Flight class
 class DepartureFlight : public Aircraft {
 private:
     DepartureState state;
     int stateTime; // Time spent in current state
     
     // State transition times (in simulation seconds)
     const int TAXI_TIME = 15;
     const int TAKEOFF_TIME = 10;
     const int CLIMB_TIME = 20;
     
 public:
     DepartureFlight(const string& flightNumber, const string& airline, FlightType type, 
                     Direction direction, int priority, 
                     chrono::system_clock::time_point scheduledTime)
         : Aircraft(flightNumber, airline, type, direction, priority, scheduledTime),
           state(DepartureState::AT_GATE), stateTime(0) {
         
         // Initial speed at gate is 0
         currentSpeed = 0;
     }
     
     DepartureState getState() const {
         return state;
     }
     
     string getStateString() const override {
         switch (state) {
             case DepartureState::AT_GATE: return "At Gate";
             case DepartureState::TAXI: return "Taxi";
             case DepartureState::TAKEOFF_ROLL: return "Takeoff Roll";
             case DepartureState::CLIMB: return "Climb";
             case DepartureState::CRUISE: return "Cruise";
             default: return "Unknown";
         }
     }
     
     // For DepartureFlight::updateStatus (around line 766)

void updateStatus(int simulationTime) override {
    stateTime++;
    
    // Store previous state to detect state transitions
    DepartureState previousState = state;
    
    // Update speed and state based on current state and time spent in that state
    switch (state) {
        case DepartureState::AT_GATE:
            if (assignedRunway != Runway::NONE) {
                state = DepartureState::TAXI;
                stateTime = 0;
                // Reset violation settings when changing state
                maintainViolationSpeed = false;
                
                if (!maintainViolationSpeed) {
                    uniform_int_distribution<> taxiDist(TAXI_MIN_SPEED, TAXI_MAX_SPEED);
                    currentSpeed = taxiDist(gen);
                }
            } else {
                currentSpeed = 0;
            }
            break;
            
        case DepartureState::TAXI:
            if (stateTime >= TAXI_TIME) {
                state = DepartureState::TAKEOFF_ROLL;
                stateTime = 0;
                // Reset violation settings when changing state
                maintainViolationSpeed = false;
                
                if (!maintainViolationSpeed) {
                    currentSpeed = 0; // Start from standstill
                }
            }
            break;
            
        case DepartureState::TAKEOFF_ROLL:
            if (!maintainViolationSpeed) {
                // Gradually increase speed during takeoff roll
                currentSpeed = min(TAKEOFF_MAX_SPEED, 
                                 (TAKEOFF_MAX_SPEED * stateTime) / TAKEOFF_TIME);
            }
            
            if (stateTime >= TAKEOFF_TIME) {
                state = DepartureState::CLIMB;
                stateTime = 0;
                // Reset violation settings when changing state
                maintainViolationSpeed = false;
                
                if (!maintainViolationSpeed) {
                    uniform_int_distribution<> climbDist(CLIMB_MIN_SPEED, CLIMB_MAX_SPEED);
                    currentSpeed = climbDist(gen);
                }
            }
            break;
            
        case DepartureState::CLIMB:
            if (stateTime >= CLIMB_TIME) {
                state = DepartureState::CRUISE;
                stateTime = 0;
                // Reset violation settings when changing state
                maintainViolationSpeed = false;
                
                if (!maintainViolationSpeed) {
                    uniform_int_distribution<> cruiseDist(CRUISE_MIN_SPEED, CRUISE_MAX_SPEED);
                    currentSpeed = cruiseDist(gen);
                }
            }
            break;
            
        case DepartureState::CRUISE:
            // Maintain cruise speed
            break;
    }
    
    // If state has changed, clear violation speed
    if (previousState != state) {
        maintainViolationSpeed = false;
    }
    
    // Randomly introduce speed violations with a configurable probability
    if (!hasActiveViolation && !isEmergency && !maintainViolationSpeed) {  // Don't give violations to emergency flights
        uniform_int_distribution<> violationChanceDist(1, 100);
        
        // Only proceed with violation logic if the random check passes
        // Make this a lower probability to ensure fewer aircraft get violations
        if (violationChanceDist(gen) <= VIOLATION_PROBABILITY / 3) {
            uniform_int_distribution<> violationDist(1, 100);
            if (violationDist(gen) <= VIOLATION_PROBABILITY) {
                // Determine excess speed based on current state
                int excessSpeed = 0;
                uniform_int_distribution<> excessDist(5, MAX_VIOLATION_SPEED_EXCESS);
                
                switch (state) {
                    case DepartureState::TAXI:
                        excessSpeed = excessDist(gen) / 2; // Less excess for taxi speeds
                        currentSpeed = TAXI_MAX_SPEED + excessSpeed;
                        maintainViolationSpeed = true;
                        violationSpeed = currentSpeed;
                        break;
                        
                    case DepartureState::TAKEOFF_ROLL:
                        if (stateTime > TAKEOFF_TIME / 2) {
                            // Only exceed speed when we're supposed to be at a moderate speed
                            excessSpeed = excessDist(gen);
                            currentSpeed = TAKEOFF_MAX_SPEED + excessSpeed;
                            maintainViolationSpeed = true;
                            violationSpeed = currentSpeed;
                        }
                        break;
                        
                    case DepartureState::CLIMB:
                        excessSpeed = excessDist(gen);
                        currentSpeed = CLIMB_MAX_SPEED + excessSpeed;
                        maintainViolationSpeed = true;
                        violationSpeed = currentSpeed;
                        break;
                        
                    case DepartureState::CRUISE:
                        // Either too slow or too fast
                        if (violationDist(gen) > 50) {
                            excessSpeed = excessDist(gen);
                            currentSpeed = CRUISE_MAX_SPEED + excessSpeed;
                        } else {
                            excessSpeed = excessDist(gen);
                            currentSpeed = CRUISE_MIN_SPEED - excessSpeed;
                        }
                        maintainViolationSpeed = true;
                        violationSpeed = currentSpeed;
                        break;
                        
                    default:
                        break;
                }
            }
        }
    } else if (maintainViolationSpeed) {
        // Maintain the violation speed until state changes
        currentSpeed = violationSpeed;
    }
    
    // Check for violations
    checkViolation();
}
     
     // Modify the checkViolation method in DepartureFlight class (around line 853)

void checkViolation() override {
    // Get current state as a string
    string currentStateStr = getStateString();
    
    // Skip violation check if we already had a violation in this state
    if (violatedStates.find(currentStateStr) != violatedStates.end()) {
        return;
    }
    
    bool violation = false;
    int minSpeed = 0, maxSpeed = 0;
    
    switch (state) {
        case DepartureState::AT_GATE:
            if (currentSpeed > GATE_MAX_SPEED) {
                violation = true;
                minSpeed = 0;
                maxSpeed = GATE_MAX_SPEED;
            }
            break;
            
        case DepartureState::TAXI:
            if (currentSpeed > TAXI_MAX_SPEED) {
                violation = true;
                minSpeed = TAXI_MIN_SPEED;
                maxSpeed = TAXI_MAX_SPEED;
            }
            break;
            
        case DepartureState::TAKEOFF_ROLL:
            if (currentSpeed > TAKEOFF_MAX_SPEED) {
                violation = true;
                minSpeed = 0;
                maxSpeed = TAKEOFF_MAX_SPEED;
            }
            break;
            
        case DepartureState::CLIMB:
            if (currentSpeed > CLIMB_MAX_SPEED) {
                violation = true;
                minSpeed = CLIMB_MIN_SPEED;
                maxSpeed = CLIMB_MAX_SPEED;
            }
            break;
            
        case DepartureState::CRUISE:
            if (currentSpeed < CRUISE_MIN_SPEED || currentSpeed > CRUISE_MAX_SPEED) {
                violation = true;
                minSpeed = CRUISE_MIN_SPEED;
                maxSpeed = CRUISE_MAX_SPEED;
            }
            break;
    }
    
    // If there's a violation
    if (violation) {
        hasActiveViolation = true;
        
        // Create new AVN
        static int avnIdCounter = 1000;
        currentViolation = make_shared<AVN>(
            avnIdCounter++, airline, flightNumber, type,
            currentSpeed, minSpeed, maxSpeed
        );
        
        // Add this state to the set of states that have had violations
        violatedStates.insert(currentStateStr);
        
        lock_guard<mutex> lock(cout_mutex);
        cout << "\nVIOLATION DETECTED! Flight " << flightNumber 
             << " (" << airline << ") - Speed: " << currentSpeed 
             << " km/h in " << getStateString() << " state.\n";
    }
}
     
     bool isCompleted() const override {
         return state == DepartureState::CRUISE;
     }
 };
 
 // Flight Scheduler
 class FlightScheduler {
 private:
     vector<shared_ptr<Aircraft>> allFlights;
     vector<shared_ptr<Aircraft>> activeFlights;
     vector<shared_ptr<Aircraft>> completedFlights;
     map<string, shared_ptr<Airline>> airlines;
     vector<shared_ptr<AVN>> allAVNs;
 
     int currentSimulationTime;
     int lastNorthArrival;
     int lastSouthArrival;
     int lastEastDeparture;
     int lastWestDeparture;
     
     // Mutexes for runway access control
     mutex runwayAMutex;
     mutex runwayBMutex;
     mutex runwayCMutex;
     
     // Runway availability flags
     bool runwayAAvailable;
     bool runwayBAvailable;
     bool runwayCAvailable;
     
     // Priority queue comparator
     struct CompareAircraftPriority {
         bool operator()(const shared_ptr<Aircraft>& a, const shared_ptr<Aircraft>& b) {
             // First by priority (higher number = higher priority)
             if (a->priority != b->priority) {
                 return a->priority < b->priority;
             }
             // Then by scheduled time
             return a->scheduledTime > b->scheduledTime;
         }
     };
     
     // Priority queues for runways
     priority_queue<shared_ptr<Aircraft>, vector<shared_ptr<Aircraft>>, CompareAircraftPriority> runwayAQueue;
     priority_queue<shared_ptr<Aircraft>, vector<shared_ptr<Aircraft>>, CompareAircraftPriority> runwayBQueue;
     priority_queue<shared_ptr<Aircraft>, vector<shared_ptr<Aircraft>>, CompareAircraftPriority> runwayCQueue;
     
     // Runway occupancy
     shared_ptr<Aircraft> runwayAOccupant;
     shared_ptr<Aircraft> runwayBOccupant;
     shared_ptr<Aircraft> runwayCOccupant;
     
     int runwayAFreeTime;
     int runwayBFreeTime;
     int runwayCFreeTime;
     
     int avnWritePipe; // Pipe to communicate with AVN Generator
     
 public:
     FlightScheduler(int avnPipe) : currentSimulationTime(0), 
     lastNorthArrival(0), lastSouthArrival(0),
     lastEastDeparture(0), lastWestDeparture(0),
     runwayAFreeTime(0), runwayBFreeTime(0), runwayCFreeTime(0),
     avnWritePipe(avnPipe),
     runwayAAvailable(true), runwayBAvailable(true), runwayCAvailable(true) {
     // Initialize airlines
     airlines["PIA"] = make_shared<Airline>("PIA", 6, 4);
     airlines["AirBlue"] = make_shared<Airline>("AirBlue", 4, 4);
     airlines["FedEx"] = make_shared<Airline>("FedEx", 3, 2);
     airlines["Pakistan Airforce"] = make_shared<Airline>("Pakistan Airforce", 2, 1);
     airlines["Blue Dart"] = make_shared<Airline>("Blue Dart", 2, 2);
     airlines["AghaKhan Air Ambulance"] = make_shared<Airline>("AghaKhan Air Ambulance", 2, 1);
     }
     
     void updateSimulation() {
         currentSimulationTime++;
         
         // Generate new flights
         generateFlights();
         
         // Assign runways
         assignRunways();
         
         // Update active flights
         updateFlights();
         
         // Move completed flights
         moveCompletedFlights();
     }
     
     int getCurrentTime() const {
         return currentSimulationTime;
     }
     
     void generateFlights() {
         // North arrivals (every 3 minutes)
         if (currentSimulationTime - lastNorthArrival >= ARRIVAL_NORTH_INTERVAL || currentSimulationTime == 1) {
             lastNorthArrival = currentSimulationTime;
             
             // Determine if this is an emergency
             uniform_int_distribution<> emergencyDist(1, 100);
             bool isEmergency = (emergencyDist(gen) <= NORTH_EMERGENCY_PROBABILITY);
             
             // Select airline randomly
             vector<string> airlineNames;
             for (const auto& pair : airlines) {
                 if (pair.second->activeFlights > 0) {
                     airlineNames.push_back(pair.first);
                 }
             }
             
             uniform_int_distribution<> airlineDist(0, airlineNames.size() - 1);
             string airline = airlineNames[airlineDist(gen)];
             
             // Determine flight type
             FlightType type = FlightType::COMMERCIAL;
             if (airline == "FedEx" || airline == "Blue Dart") {
                 type = FlightType::CARGO;
             }
             if (isEmergency || airline == "Pakistan Airforce") {
                 type = FlightType::EMERGENCY;
             }
             
             // Create flight number
             stringstream ssFlightNum;
             ssFlightNum << airline.substr(0, 2) << "-" << (1000 + allFlights.size());
             string flightNumber = ssFlightNum.str();
             
             // Set priority (emergency = 3, cargo = 2, commercial = 1)
             int priority = (isEmergency) ? 3 : ((type == FlightType::CARGO) ? 2 : 1);
             
             // Create arrival flight
             auto flight = make_shared<ArrivalFlight>(
                 flightNumber, airline, type, Direction::NORTH, priority,
                 chrono::system_clock::now()
             );
             flight->isEmergency = isEmergency;
             
             allFlights.push_back(flight);
             activeFlights.push_back(flight);
             
             // Add to runway A queue (north arrivals)
             runwayAQueue.push(flight);
             
             lock_guard<mutex> lock(cout_mutex);
             cout << "\nNew North Arrival: " << flight->getSummary() << endl;
         }
         
         // South arrivals (every 2 minutes)
         if (currentSimulationTime - lastSouthArrival >= ARRIVAL_SOUTH_INTERVAL || currentSimulationTime == 2) {
             lastSouthArrival = currentSimulationTime;
             
             // Determine if this is an emergency
             uniform_int_distribution<> emergencyDist(1, 100);
             bool isEmergency = (emergencyDist(gen) <= SOUTH_EMERGENCY_PROBABILITY);
             
             // Select airline randomly
             vector<string> airlineNames;
             for (const auto& pair : airlines) {
                 if (pair.second->activeFlights > 0) {
                     airlineNames.push_back(pair.first);
                 }
             }
             
             uniform_int_distribution<> airlineDist(0, airlineNames.size() - 1);
             string airline = airlineNames[airlineDist(gen)];
             
             // Determine flight type
             FlightType type = FlightType::COMMERCIAL;
             if (airline == "FedEx" || airline == "Blue Dart") {
                 type = FlightType::CARGO;
             }
             if (isEmergency || airline == "AghaKhan Air Ambulance") {
                 type = FlightType::EMERGENCY;
             }
             
             // Create flight number
             stringstream ssFlightNum;
             ssFlightNum << airline.substr(0, 2) << "-" << (1000 + allFlights.size());
             string flightNumber = ssFlightNum.str();
             
             // Set priority (emergency = 3, cargo = 2, commercial = 1)
             int priority = (isEmergency) ? 3 : ((type == FlightType::CARGO) ? 2 : 1);
                         
             // Create arrival flight
             auto flight = make_shared<ArrivalFlight>(
                 flightNumber, airline, type, Direction::SOUTH, priority,
                 chrono::system_clock::now()
             );
             flight->isEmergency = isEmergency;
             
             allFlights.push_back(flight);
             activeFlights.push_back(flight);
             
             // Add to runway A queue (south arrivals)
             runwayAQueue.push(flight);
             
             lock_guard<mutex> lock(cout_mutex);
             cout << "\nNew South Arrival: " << flight->getSummary() << endl;
         }
         
         // East departures (every 2.5 minutes)
         if (currentSimulationTime - lastEastDeparture >= DEPARTURE_EAST_INTERVAL || currentSimulationTime == 3) {
             lastEastDeparture = currentSimulationTime;
             
             // Determine if this is an emergency
             uniform_int_distribution<> emergencyDist(1, 100);
             bool isEmergency = (emergencyDist(gen) <= EAST_EMERGENCY_PROBABILITY);
             
             // Select airline randomly
             vector<string> airlineNames;
             for (const auto& pair : airlines) {
                 if (pair.second->activeFlights > 0) {
                     airlineNames.push_back(pair.first);
                 }
             }
             
             uniform_int_distribution<> airlineDist(0, airlineNames.size() - 1);
             string airline = airlineNames[airlineDist(gen)];
             
             // Determine flight type
             FlightType type = FlightType::COMMERCIAL;
             if (airline == "FedEx" || airline == "Blue Dart") {
                 type = FlightType::CARGO;
             }
             if (isEmergency || airline == "Pakistan Airforce") {
                 type = FlightType::EMERGENCY;
             }
             
             // Create flight number
             stringstream ssFlightNum;
             ssFlightNum << airline.substr(0, 2) << "-" << (2000 + allFlights.size());
             string flightNumber = ssFlightNum.str();
             
             // Set priority (emergency = 3, cargo = 2, commercial = 1)
             int priority = (isEmergency) ? 3 : ((type == FlightType::CARGO) ? 2 : 1);
             
             // Create departure flight
             auto flight = make_shared<DepartureFlight>(
                 flightNumber, airline, type, Direction::EAST, priority,
                 chrono::system_clock::now()
             );
             flight->isEmergency = isEmergency;
             
             allFlights.push_back(flight);
             activeFlights.push_back(flight);
             
             // Add to runway B queue (east departures)
             runwayBQueue.push(flight);
             
             lock_guard<mutex> lock(cout_mutex);
             cout << "\nNew East Departure: " << flight->getSummary() << endl;
         }
         
         // West departures (every 4 minutes)
         if (currentSimulationTime - lastWestDeparture >= DEPARTURE_WEST_INTERVAL || currentSimulationTime == 4) {
             lastWestDeparture = currentSimulationTime;
             
             // Determine if this is an emergency
             uniform_int_distribution<> emergencyDist(1, 100);
             bool isEmergency = (emergencyDist(gen) <= WEST_EMERGENCY_PROBABILITY);
             
             // Select airline randomly
             vector<string> airlineNames;
             for (const auto& pair : airlines) {
                 if (pair.second->activeFlights > 0) {
                     airlineNames.push_back(pair.first);
                 }
             }
             
             uniform_int_distribution<> airlineDist(0, airlineNames.size() - 1);
             string airline = airlineNames[airlineDist(gen)];
             
             // Determine flight type
             FlightType type = FlightType::COMMERCIAL;
             if (airline == "FedEx" || airline == "Blue Dart") {
                 type = FlightType::CARGO;
             }
             if (isEmergency) {
                 type = FlightType::EMERGENCY;
             }
             
             // Create flight number
             stringstream ssFlightNum;
             ssFlightNum << airline.substr(0, 2) << "-" << (2000 + allFlights.size());
             string flightNumber = ssFlightNum.str();
             
             // Set priority (emergency = 3, cargo = 2, commercial = 1)
             int priority = (isEmergency) ? 3 : ((type == FlightType::CARGO) ? 2 : 1);
             
             // Create departure flight
             auto flight = make_shared<DepartureFlight>(
                 flightNumber, airline, type, Direction::WEST, priority,
                 chrono::system_clock::now()
             );
             flight->isEmergency = isEmergency;
             
             allFlights.push_back(flight);
             activeFlights.push_back(flight);
             
             // Add to runway B queue (west departures)
             runwayBQueue.push(flight);
             
             lock_guard<mutex> lock(cout_mutex);
             cout << "\nNew West Departure: " << flight->getSummary() << endl;
         }
     }
     
     void assignRunways() {
         // Temporary queues to hold unassigned flights
         priority_queue<shared_ptr<Aircraft>, vector<shared_ptr<Aircraft>>, CompareAircraftPriority> tempAQueue;
         priority_queue<shared_ptr<Aircraft>, vector<shared_ptr<Aircraft>>, CompareAircraftPriority> tempBQueue;
         priority_queue<shared_ptr<Aircraft>, vector<shared_ptr<Aircraft>>, CompareAircraftPriority> tempCQueue;
     
         // Process runway A queue (North/South arrivals)
         while (!runwayAQueue.empty()) {
             shared_ptr<Aircraft> aircraft = runwayAQueue.top();
             runwayAQueue.pop();
     
             // Skip if already assigned a runway
             if (aircraft->assignedRunway != Runway::NONE) {
                 tempAQueue.push(aircraft);
                 continue;
             }
     
             bool assigned = false;
     
             // Try RWY-C for emergency or cargo flights
             if (aircraft->type == FlightType::EMERGENCY || aircraft->type == FlightType::CARGO) {
                 lock_guard<mutex> lock(runwayCMutex);
                 if (runwayCAvailable && currentSimulationTime >= runwayCFreeTime) {
                     runwayCAvailable = false;
                     runwayCOccupant = aircraft;
                     aircraft->assignedRunway = Runway::RWY_C;
                     assigned = true;
                     lock_guard<mutex> coutLock(cout_mutex);
                     cout << "Assigned RWY-C to " << aircraft->flightNumber << " (" << aircraft->airline << ")" << endl;
                 }
             }
     
             // Try RWY-A for North/South arrivals
             if (!assigned && (aircraft->direction == Direction::NORTH || aircraft->direction == Direction::SOUTH)) {
                 lock_guard<mutex> lock(runwayAMutex);
                 if (runwayAAvailable && currentSimulationTime >= runwayAFreeTime) {
                     runwayAAvailable = false;
                     runwayAOccupant = aircraft;
                     aircraft->assignedRunway = Runway::RWY_A;
                     assigned = true;
                     lock_guard<mutex> coutLock(cout_mutex);
                     cout << "Assigned RWY-A to " << aircraft->flightNumber << " (" << aircraft->airline << ")" << endl;
                 }
             }
     
             // Try RWY-C as fallback for non-cargo flights
             if (!assigned && aircraft->type != FlightType::CARGO) {
                 lock_guard<mutex> lock(runwayCMutex);
                 if (runwayCAvailable && currentSimulationTime >= runwayCFreeTime) {
                     runwayCAvailable = false;
                     runwayCOccupant = aircraft;
                     aircraft->assignedRunway = Runway::RWY_C;
                     assigned = true;
                     lock_guard<mutex> coutLock(cout_mutex);
                     cout << "Assigned RWY-C (fallback) to " << aircraft->flightNumber << " (" << aircraft->airline << ")" << endl;
                 }
             }
     
             // If not assigned, re-queue
             if (!assigned) {
                 tempAQueue.push(aircraft);
             }
         }
     
         // Process runway B queue (East/West departures)
         while (!runwayBQueue.empty()) {
             shared_ptr<Aircraft> aircraft = runwayBQueue.top();
             runwayBQueue.pop();
     
             // Skip if already assigned a runway
             if (aircraft->assignedRunway != Runway::NONE) {
                 tempBQueue.push(aircraft);
                 continue;
             }
     
             bool assigned = false;
     
             // Try RWY-C for emergency or cargo flights
             if (aircraft->type == FlightType::EMERGENCY || aircraft->type == FlightType::CARGO) {
                 lock_guard<mutex> lock(runwayCMutex);
                 if (runwayCAvailable && currentSimulationTime >= runwayCFreeTime) {
                     runwayCAvailable = false;
                     runwayCOccupant = aircraft;
                     aircraft->assignedRunway = Runway::RWY_C;
                     assigned = true;
                     lock_guard<mutex> coutLock(cout_mutex);
                     cout << "Assigned RWY-C to " << aircraft->flightNumber << " (" << aircraft->airline << ")" << endl;
                 }
             }
     
             // Try RWY-B for East/West departures
             if (!assigned && (aircraft->direction == Direction::EAST || aircraft->direction == Direction::WEST)) {
                 lock_guard<mutex> lock(runwayBMutex);
                 if (runwayBAvailable && currentSimulationTime >= runwayBFreeTime) {
                     runwayBAvailable = false;
                     runwayBOccupant = aircraft;
                     aircraft->assignedRunway = Runway::RWY_B;
                     assigned = true;
                     lock_guard<mutex> coutLock(cout_mutex);
                     cout << "Assigned RWY-B to " << aircraft->flightNumber << " (" << aircraft->airline << ")" << endl;
                 }
             }
     
             // Try RWY-C as fallback for non-cargo flights
             if (!assigned && aircraft->type != FlightType::CARGO) {
                 lock_guard<mutex> lock(runwayCMutex);
                 if (runwayCAvailable && currentSimulationTime >= runwayCFreeTime) {
                     runwayCAvailable = false;
                     runwayCOccupant = aircraft;
                     aircraft->assignedRunway = Runway::RWY_C;
                     assigned = true;
                     lock_guard<mutex> coutLock(cout_mutex);
                     cout << "Assigned RWY-C (fallback) to " << aircraft->flightNumber << " (" << aircraft->airline << ")" << endl;
                 }
             }
     
             // If not assigned, re-queue
             if (!assigned) {
                 tempBQueue.push(aircraft);
             }
         }
     
         // Process runway C queue (emergency/cargo overflow)
         while (!runwayCQueue.empty()) {
             shared_ptr<Aircraft> aircraft = runwayCQueue.top();
             runwayCQueue.pop();
     
             // Skip if already assigned a runway
             if (aircraft->assignedRunway != Runway::NONE) {
                 tempCQueue.push(aircraft);
                 continue;
             }
     
             bool assigned = false;
     
             // Try RWY-C for all flights in this queue
             lock_guard<mutex> lock(runwayCMutex);
             if (runwayCAvailable && currentSimulationTime >= runwayCFreeTime) {
                 runwayCAvailable = false;
                 runwayCOccupant = aircraft;
                 aircraft->assignedRunway = Runway::RWY_C;
                 assigned = true;
                 lock_guard<mutex> coutLock(cout_mutex);
                 cout << "Assigned RWY-C to " << aircraft->flightNumber << " (" << aircraft->airline << ")" << endl;
             }
     
             // If not assigned, re-queue
             if (!assigned) {
                 tempCQueue.push(aircraft);
             }
         }
     
         // Restore unassigned flights to queues
         runwayAQueue = tempAQueue;
         runwayBQueue = tempBQueue;
         runwayCQueue = tempCQueue;
     
         // Check for runway release
         for (auto& flight : activeFlights) {
             if (flight->assignedRunway != Runway::NONE) {
                 bool releaseRunway = false;
     
                 // Check arrival flights
                 if (auto arrival = dynamic_pointer_cast<ArrivalFlight>(flight)) {
                     if (arrival->getState() == ArrivalState::TAXI || 
                         arrival->getState() == ArrivalState::AT_GATE) {
                         releaseRunway = true;
                     }
                 }
     
                 // Check departure flights
                 if (auto departure = dynamic_pointer_cast<DepartureFlight>(flight)) {
                     if (departure->getState() == DepartureState::CLIMB || 
                         departure->getState() == DepartureState::CRUISE) {
                         releaseRunway = true;
                     }
                 }
     
                 if (releaseRunway) {
                     Runway runway = flight->assignedRunway;
                     flight->assignedRunway = Runway::NONE;
     
                     if (runway == Runway::RWY_A) {
                         lock_guard<mutex> lock(runwayAMutex);
                         runwayAAvailable = true;
                         runwayAOccupant = nullptr;
                         runwayAFreeTime = currentSimulationTime;
                         lock_guard<mutex> coutLock(cout_mutex);
                         cout << "Released RWY-A from " << flight->flightNumber << " (" << flight->airline << ")" << endl;
                     } else if (runway == Runway::RWY_B) {
                         lock_guard<mutex> lock(runwayBMutex);
                         runwayBAvailable = true;
                         runwayBOccupant = nullptr;
                         runwayBFreeTime = currentSimulationTime;
                         lock_guard<mutex> coutLock(cout_mutex);
                         cout << "Released RWY-B from " << flight->flightNumber << " (" << flight->airline << ")" << endl;
                     } else if (runway == Runway::RWY_C) {
                         lock_guard<mutex> lock(runwayCMutex);
                         runwayCAvailable = true;
                         runwayCOccupant = nullptr;
                         runwayCFreeTime = currentSimulationTime;
                         lock_guard<mutex> coutLock(cout_mutex);
                         cout << "Released RWY-C from " << flight->flightNumber << " (" << flight->airline << ")" << endl;
                     }
                 }
             }
         }
     }
     
     void updateFlights() {
         for (auto& flight : activeFlights) {
             flight->updateStatus(currentSimulationTime);
             
             // Check if flight has active violation
             if (flight->hasActiveViolation && flight->currentViolation) {
                 // Add violation to airline's record
                 auto airlineIt = airlines.find(flight->airline);
                 if (airlineIt != airlines.end()) {
                     airlineIt->second->addViolation(flight->currentViolation);
                     
                     // Add to the global list of AVNs
                     allAVNs.push_back(flight->currentViolation);
                     
                     // Notify AVN Generator with a new IPC message
                     IPCMessage message;
                     message.type = MessageType::AVN_CREATED;
                     message.avnId = flight->currentViolation->id;
                     strncpy(message.airline, flight->airline.c_str(), sizeof(message.airline) - 1);
                     message.airline[sizeof(message.airline) - 1] = '\0';
                     strncpy(message.flightNumber, flight->flightNumber.c_str(), sizeof(message.flightNumber) - 1);
                     message.flightNumber[sizeof(message.flightNumber) - 1] = '\0';
                     message.amount = flight->currentSpeed;  // Send current speed as amount
                     message.minSpeed = flight->currentViolation->permissibleSpeedMin;
                     message.maxSpeed = flight->currentViolation->permissibleSpeedMax;
                     strncpy(message.details, (flight->type == FlightType::COMMERCIAL) ? "COMMERCIAL" : "CARGO", sizeof(message.details) - 1);
                     message.details[sizeof(message.details) - 1] = '\0';
                     
                     write(avnWritePipe, &message, sizeof(message));
                     
                     // Reset violation flag and clear current violation
                     flight->hasActiveViolation = false;
                     flight->currentViolation.reset();
                 }
             }
         }
     }
     
     void moveCompletedFlights() {
         vector<shared_ptr<Aircraft>> stillActive;
         
         for (auto& flight : activeFlights) {
             if (flight->isCompleted()) {
                 completedFlights.push_back(flight);
                 
                 lock_guard<mutex> lock(cout_mutex);
                 cout << "\nFlight completed: " << flight->flightNumber 
                      << " (" << flight->airline << ")" << endl;
             } else {
                 stillActive.push_back(flight);
             }
         }
         
         // Update active flights list
         activeFlights = stillActive;
     }
     
     void printStatus() {
         lock_guard<mutex> lock(cout_mutex);
         
         cout << "\n======== AIRCONTROLX STATUS ========" << endl;
         cout << "Simulation Time: " << currentSimulationTime << " seconds" << endl;
         cout << "Active Flights: " << activeFlights.size() << endl;
         cout << "Completed Flights: " << completedFlights.size() << endl;
         
         // Runway status
         cout << "\n--- RUNWAY STATUS ---" << endl;
         cout << "Runway A: " << (runwayAOccupant ? runwayAOccupant->flightNumber + " (" + runwayAOccupant->airline + ")" : "Free") << endl;
         cout << "Runway B: " << (runwayBOccupant ? runwayBOccupant->flightNumber + " (" + runwayBOccupant->airline + ")" : "Free") << endl;
         cout << "Runway C: " << (runwayCOccupant ? runwayCOccupant->flightNumber + " (" + runwayCOccupant->airline + ")" : "Free") << endl;
         
         // Queue status
         cout << "\n--- QUEUE STATUS ---" << endl;
         cout << "Runway A Queue: " << runwayAQueue.size() << " flights waiting" << endl;
         cout << "Runway B Queue: " << runwayBQueue.size() << " flights waiting" << endl;
         
         // Active flights
         cout << "\n--- ACTIVE FLIGHTS ---" << endl;
         for (const auto& flight : activeFlights) {
             cout << flight->getSummary() << endl;
         }
         
         // Active violations
        //  cout << "\n--- ACTIVE VIOLATIONS ---" << endl;
        //  bool hasViolations = false;
        //  for (const auto& flight : activeFlights) {
        //      if (flight->hasActiveViolation) {
        //          cout << "Flight " << flight->flightNumber << " (" << flight->airline << ") - Speed: " 
        //               << flight->currentSpeed << " km/h" << endl;
        //          hasViolations = true;
        //      }
        //  }
         
        //  if (!hasViolations) {
        //      cout << "No active violations." << endl;
        //  }
        
        cout << "\n--- ACTIVE AVNs ---" << endl;
        if (allAVNs.empty()) {
            cout << "No AVNs issued yet." << endl;
        } else {
            bool hasUnpaidAVNs = false;
            for (const auto& avn : allAVNs) {
                if (avn->status == PaymentStatus::UNPAID) {
                    cout << "AVN #" << avn->id << " | " << avn->airline << " flight " << avn->flightNumber 
                        << " | Speed: " << avn->recordedSpeed << " km/h"
                        << " | Amount: PKR " << fixed << setprecision(2) << avn->totalAmount << endl;
                    hasUnpaidAVNs = true;
                }
            }
            
            if (!hasUnpaidAVNs) {
                cout << "All AVNs have been paid." << endl;
            }
        }
         cout << "=====================================" << endl;
     }
     
     void processAVNPayment(int avnId, double amount) {
         for (auto& avn : allAVNs) {
             if (avn->id == avnId) {
                 if (amount >= avn->totalAmount) {
                     avn->status = PaymentStatus::PAID;
                     
                     lock_guard<mutex> lock(cout_mutex);
                     cout << "\nPayment processed for AVN #" << avnId << " - PKR " << fixed << setprecision(2) << amount << endl;
                     cout << "AVN status updated to PAID." << endl;
                 } else {
                     lock_guard<mutex> lock(cout_mutex);
                     cout << "\nInsufficient payment for AVN #" << avnId << ". Required: PKR " << fixed << setprecision(2) << avn->totalAmount << endl;
                 }
                 return;
             }
         }
         
         lock_guard<mutex> lock(cout_mutex);
         cout << "\nAVN #" << avnId << " not found." << endl;
     }
     
     void displayAVNDetails(int avnId) {
         for (const auto& avn : allAVNs) {
             if (avn->id == avnId) {
                 avn->printDetails();
                 return;
             }
         }
         
         lock_guard<mutex> lock(cout_mutex);
         cout << "\nAVN #" << avnId << " not found." << endl;
     }
     
     void displayAirlineViolations(const string& airlineName) {
         auto airlineIt = airlines.find(airlineName);
         if (airlineIt != airlines.end()) {
             airlineIt->second->printViolations();
         } else {
             lock_guard<mutex> lock(cout_mutex);
             cout << "\nAirline '" << airlineName << "' not found." << endl;
         }
     }
     
     const vector<shared_ptr<AVN>>& getAllAVNs() const {
         return allAVNs;
     }
     
     const map<string, shared_ptr<Airline>>& getAirlines() const {
         return airlines;
     }
 };
 
 // AVN Generator Process
 class AVNGenerator {
 private:
     vector<shared_ptr<AVN>> avns;
     int nextAVNId;
     int readPipe;
     int writePipe;
     
 public:
     AVNGenerator(int read, int write) 
         : nextAVNId(1000), readPipe(read), writePipe(write) {}
     
     void run() {
         while (true) {
             // Read messages from pipe
             IPCMessage message;
             ssize_t bytesRead = read(readPipe, &message, sizeof(message));
             
             if (bytesRead <= 0) {
                 // Error or pipe closed
                 break;
             }
             
             processMessage(message);
         }
     }
     
     void processMessage(const IPCMessage& message) {
         switch (message.type) {
             case MessageType::AVN_CREATED: {
                 // Create a new AVN based on the message
                 FlightType flightType = (strncmp(message.details, "COMMERCIAL", sizeof(message.details)) == 0) ? 
                                        FlightType::COMMERCIAL : FlightType::CARGO;
                 
                 // Create a new AVN with proper speed information
                 auto newAVN = make_shared<AVN>(
                     nextAVNId++,
                     string(message.airline),
                     string(message.flightNumber),
                     flightType,
                     static_cast<int>(message.amount),  // Recorded speed
                     message.minSpeed,  // Permissible min speed
                     message.maxSpeed   // Permissible max speed
                 );
                 
                 // Store the AVN
                 avns.push_back(newAVN);
                 
                 // Send notification to Airline Portal
                 IPCMessage response;
                 response.type = MessageType::AVN_CREATED;
                 response.avnId = newAVN->id;
                 strncpy(response.airline, newAVN->airline.c_str(), sizeof(response.airline) - 1);
                 response.airline[sizeof(response.airline) - 1] = '\0';
                 strncpy(response.flightNumber, newAVN->flightNumber.c_str(), sizeof(response.flightNumber) - 1);
                 response.flightNumber[sizeof(response.flightNumber) - 1] = '\0';
                 response.amount = newAVN->totalAmount;
                 strncpy(response.details, (newAVN->status == PaymentStatus::PAID) ? "PAID" : "UNPAID", sizeof(response.details) - 1);
                 response.details[sizeof(response.details) - 1] = '\0';
                 
                 write(writePipe, &response, sizeof(response));
                 
                 lock_guard<mutex> lock(cout_mutex);
                 cout << "[AVN Generator] Created AVN #" << newAVN->id << " for " 
                      << newAVN->airline << " flight " << newAVN->flightNumber 
                      << " - PKR " << fixed << setprecision(2) << newAVN->totalAmount << endl;
                 break;
             }
                 
             case MessageType::PAYMENT_CONFIRMATION: {
                 // Find the AVN and update its status
                 for (auto& avn : avns) {
                     if (avn->id == message.avnId) {
                         avn->status = PaymentStatus::PAID;
                         
                         // Send confirmation to Airline Portal
                         IPCMessage response;
                         response.type = MessageType::PAYMENT_CONFIRMATION;
                         response.avnId = avn->id;
                         strncpy(response.airline, avn->airline.c_str(), sizeof(response.airline) - 1);
                         response.airline[sizeof(response.airline) - 1] = '\0';
                         response.amount = message.amount;
                         
                         write(writePipe, &response, sizeof(response));
                         
                         lock_guard<mutex> lock(cout_mutex);
                         cout << "[AVN Generator] Payment confirmed for AVN #" << avn->id 
                              << " - PKR " << fixed << setprecision(2) << message.amount << endl;
                         break;
                     }
                 }
                 break;
             }
                 
             case MessageType::QUERY_AVN: {
                 // Find the AVN and send its details
                 for (const auto& avn : avns) {
                     if (avn->id == message.avnId) {
                         IPCMessage response;
                         response.type = MessageType::QUERY_AVN;
                         response.avnId = avn->id;
                         strncpy(response.airline, avn->airline.c_str(), sizeof(response.airline) - 1);
                         response.airline[sizeof(response.airline) - 1] = '\0';
                         strncpy(response.flightNumber, avn->flightNumber.c_str(), sizeof(response.flightNumber) - 1);
                         response.flightNumber[sizeof(response.flightNumber) - 1] = '\0';
                         response.amount = avn->totalAmount;
                         strncpy(response.details, (avn->status == PaymentStatus::PAID) ? "PAID" : "UNPAID", sizeof(response.details) - 1);
                         response.details[sizeof(response.details) - 1] = '\0';
                         
                         write(writePipe, &response, sizeof(response));
                         break;
                     }
                 }
                 break;
             }
                 
             case MessageType::QUERY_AIRLINE: {
                 // Find all AVNs for the airline
                 stringstream ss;
                 int count = 0;
                 
                 for (const auto& avn : avns) {
                     if (avn->airline == string(message.airline)) {
                         ss << "AVN #" << avn->id << " | " << avn->flightNumber 
                            << " | PKR " << fixed << setprecision(2) << avn->totalAmount 
                            << " | " << ((avn->status == PaymentStatus::PAID) ? "PAID" : "UNPAID") << "\n";
                         count++;
                     }
                 }
                 
                 IPCMessage response;
                 response.type = MessageType::QUERY_AIRLINE;
                 strncpy(response.airline, message.airline, sizeof(response.airline) - 1);
                 response.airline[sizeof(response.airline) - 1] = '\0';
                 string detailsStr = ss.str();
                 strncpy(response.details, detailsStr.c_str(), sizeof(response.details) - 1);
                 response.details[sizeof(response.details) - 1] = '\0';
                 
                 write(writePipe, &response, sizeof(response));
                 
                 lock_guard<mutex> lock(cout_mutex);
                 cout << "[AVN Generator] Queried " << count << " AVNs for " << message.airline << endl;
                 break;
             }
                 
             default:
                 break;
         }
     }
 };
 
 // Airline Portal Process
 class AirlinePortal {
 private:
     int readPipe;
     int writePipe;
     int stripePayPipe;
     map<string, vector<shared_ptr<AVN>>> airlineAVNs;
     
 public:
     AirlinePortal(int read, int write, int stripePay) 
         : readPipe(read), writePipe(write), stripePayPipe(stripePay) {}
     
     void run() {
         while (true) {
             displayMenu();
             
             int choice;
             cin >> choice;
             
             switch (choice) {
                 case 1:
                     viewAirlineAVNs();
                     break;
                     
                 case 2:
                     payAVN();
                     break;
                     
                 case 3:
                     viewAVNDetails();
                     break;
                     
                 case 4:
                     cout << "Exiting Airline Portal." << endl;
                     return;
                     
                 default:
                     cout << "Invalid choice. Please try again." << endl;
                     break;
             }
             
             // Process any incoming messages
             processIncomingMessages();
         }
     }
     
     void displayMenu() {
         lock_guard<mutex> lock(cout_mutex);
         cout << "\n===== AIRLINE PORTAL =====\n";
         cout << "1. View Airline AVNs\n";
         cout << "2. Pay AVN\n";
         cout << "3. View AVN Details\n";
         cout << "4. Exit\n";
         cout << "Enter your choice: ";
     }
     
     void viewAirlineAVNs() {
         string airline;
         lock_guard<mutex> lock(cout_mutex);
         cout << "Enter airline name: ";
         cin >> airline;
         
         // Request AVNs for the airline
         IPCMessage request;
         request.type = MessageType::QUERY_AIRLINE;
         strncpy(request.airline, airline.c_str(), sizeof(request.airline) - 1);
         request.airline[sizeof(request.airline) - 1] = '\0';
         
         write(writePipe, &request, sizeof(request));
         
         // Wait for response
         sleep(1);
         processIncomingMessages();
     }
     
     void payAVN() {
         int avnId;
         lock_guard<mutex> lock(cout_mutex);
         cout << "Enter AVN ID to pay: ";
         cin >> avnId;
         
         // Query AVN details first
         IPCMessage request;
         request.type = MessageType::QUERY_AVN;
         request.avnId = avnId;
         
         write(writePipe, &request, sizeof(request));
         
         // Wait for response
         sleep(1);
         processIncomingMessages();
         
         // Now request payment
         double amount;
         lock_guard<mutex> lock2(cout_mutex);
         cout << "Enter payment amount (PKR): ";
         cin >> amount;
         
         // Send payment request to StripePay
         IPCMessage paymentRequest;
         paymentRequest.type = MessageType::PAYMENT_REQUEST;
         paymentRequest.avnId = avnId;
         paymentRequest.amount = amount;
         
         write(stripePayPipe, &paymentRequest, sizeof(paymentRequest));
         
         lock_guard<mutex> lock3(cout_mutex);
         cout << "Payment request sent for AVN #" << avnId << " - PKR " << fixed << setprecision(2) << amount << endl;
     }
     
     void viewAVNDetails() {
         int avnId;
         lock_guard<mutex> lock(cout_mutex);
         cout << "Enter AVN ID: ";
         cin >> avnId;
         
         // Request AVN details
         IPCMessage request;
         request.type = MessageType::QUERY_AVN;
         request.avnId = avnId;
         
         write(writePipe, &request, sizeof(request));
         
         // Wait for response
         sleep(1);
         processIncomingMessages();
     }
     
     void processIncomingMessages() {
         // Check if there are messages to read
         fd_set readSet;
         FD_ZERO(&readSet);
         FD_SET(readPipe, &readSet);
         
         struct timeval timeout;
         timeout.tv_sec = 0;
         timeout.tv_usec = 100000; // 100ms timeout
         
         while (select(readPipe + 1, &readSet, NULL, NULL, &timeout) > 0) {
             IPCMessage message;
             ssize_t bytesRead = read(readPipe, &message, sizeof(message));
             
             if (bytesRead <= 0) {
                 break;
             }
             
             lock_guard<mutex> lock(cout_mutex);
             switch (message.type) {
                 case MessageType::AVN_CREATED:
                     cout << "\n[Airline Portal] New AVN #" << message.avnId << " created for " 
                          << message.airline << " flight " << message.flightNumber 
                          << " - PKR " << fixed << setprecision(2) << message.amount << endl;
                     break;
                     
                 case MessageType::PAYMENT_CONFIRMATION:
                     cout << "\n[Airline Portal] Payment confirmed for AVN #" << message.avnId 
                          << " - PKR " << fixed << setprecision(2) << message.amount << endl;
                     break;
                     
                 case MessageType::QUERY_AVN:
                     cout << "\n===== AVN #" << message.avnId << " =====\n";
                     cout << "Airline: " << message.airline << endl;
                     cout << "Flight: " << message.flightNumber << endl;
                     cout << "Amount: PKR " << fixed << setprecision(2) << message.amount << endl;
                     cout << "Status: " << message.details << endl;
                     cout << "========================" << endl;
                     break;
                     
                 case MessageType::QUERY_AIRLINE:
                     cout << "\n===== AVNs for " << message.airline << " =====\n";
                     if (message.details[0] == '\0') {
                         cout << "No AVNs found for this airline." << endl;
                     } else {
                         cout << message.details;
                     }
                     cout << "========================" << endl;
                     break;
                     
                 default:
                     break;
             }
             
             // Check if there are more messages
             FD_ZERO(&readSet);
             FD_SET(readPipe, &readSet);
             timeout.tv_sec = 0;
             timeout.tv_usec = 100000;
         }
     }
 };
 
 // StripePay Process
 class StripePay {
 private:
     int readPipe;
     int writePipe;
     
 public:
     StripePay(int read, int write) : readPipe(read), writePipe(write) {}
     
     void run() {
         while (true) {
             // Read payment requests
             IPCMessage message;
             ssize_t bytesRead = read(readPipe, &message, sizeof(message));
             
             if (bytesRead <= 0) {
                 // Error or pipe closed
                 break;
             }
             
             if (message.type == MessageType::PAYMENT_REQUEST) {
                 processPayment(message);
             }
         }
     }
     
     void processPayment(const IPCMessage& request) {
         lock_guard<mutex> lock(cout_mutex);
         cout << "[StripePay] Processing payment for AVN #" << request.avnId 
              << " - PKR " << fixed << setprecision(2) << request.amount << endl;
         
         // Simulate payment processing
         sleep(2);
         
         // Send confirmation
         IPCMessage confirmation;
         confirmation.type = MessageType::PAYMENT_CONFIRMATION;
         confirmation.avnId = request.avnId;
         confirmation.amount = request.amount;
         
         write(writePipe, &confirmation, sizeof(confirmation));
         
         lock_guard<mutex> lock2(cout_mutex);
         cout << "[StripePay] Payment confirmed for AVN #" << request.avnId 
              << " - PKR " << fixed << setprecision(2) << request.amount << endl;
     }
 };
 
 // Main function
 // Fix the main function to properly manage simulation vs airline portal modes

int main() {
    // Create pipes for IPC
    int atcToAvn[2]; // ATC -> AVN Generator
    int avnToAirline[2]; // AVN Generator -> Airline Portal
    int airlineToAvn[2]; // Airline Portal -> AVN Generator
    int airlineToStripe[2]; // Airline Portal -> StripePay
    int stripeToAvn[2]; // StripePay -> AVN Generator

    if (pipe(atcToAvn) == -1 || pipe(avnToAirline) == -1 || pipe(airlineToAvn) == -1 ||
        pipe(airlineToStripe) == -1 || pipe(stripeToAvn) == -1) {
        cerr << "Pipe creation failed!" << endl;
        return 1;
    }

    // Fork AVN Generator process
    pid_t avnPid = fork();
    if (avnPid == 0) {
        // Child process: AVN Generator
        close(atcToAvn[1]);
        close(avnToAirline[0]);
        close(airlineToAvn[1]);
        close(airlineToStripe[0]);
        close(airlineToStripe[1]);
        close(stripeToAvn[0]);
        close(stripeToAvn[1]);

        AVNGenerator avnGenerator(atcToAvn[0], avnToAirline[1]);
        avnGenerator.run();
        exit(0);
    } else if (avnPid < 0) {
        cerr << "Failed to fork AVN Generator process" << endl;
        return 1;
    }

    // Fork StripePay process
    pid_t stripePid = fork();
    if (stripePid == 0) {
        // Child process: StripePay
        close(atcToAvn[0]);
        close(atcToAvn[1]);
        close(avnToAirline[0]);
        close(avnToAirline[1]);
        close(airlineToAvn[0]);
        close(airlineToAvn[1]);
        close(airlineToStripe[1]);
        close(stripeToAvn[0]);

        StripePay stripePay(airlineToStripe[0], stripeToAvn[1]);
        stripePay.run();
        exit(0);
    } else if (stripePid < 0) {
        cerr << "Failed to fork StripePay process" << endl;
        kill(avnPid, SIGTERM);
        waitpid(avnPid, nullptr, 0);
        return 1;
    }

    // Parent process: ATC Controller
    close(avnToAirline[0]);
    close(airlineToAvn[1]);
    close(airlineToStripe[0]);
    close(stripeToAvn[0]);
    close(stripeToAvn[1]);
    
    // We'll fork a separate process for Airline Portal if needed
    pid_t airlinePid = -1;

    // Create FlightScheduler
    FlightScheduler scheduler(atcToAvn[1]);
    
    // Current simulation time
    int simulationTime = 0;
    const int MAX_SIMULATION_TIME = SIMULATION_TIME;
    
    bool continueProgram = true;
    
    while (continueProgram) {
        // Display main menu
        system("clear"); // Clear screen
        cout << "╔══════════════════════════════════════╗" << endl;
        cout << "║         AIRCONTROLX SYSTEM           ║" << endl;
        cout << "╠══════════════════════════════════════╣" << endl;
        cout << "║ 1. Run Air Traffic Simulation        ║" << endl;
        cout << "║ 2. View & Pay AVNs                   ║" << endl;
        cout << "║ 3. View Airline Violations           ║" << endl;
        cout << "║ 4. Exit                              ║" << endl;
        cout << "╚══════════════════════════════════════╝" << endl;
        cout << "Select an option: ";
        
        int choice;
        cin >> choice;
        
        switch (choice) {
            case 1: {
                // Run simulation mode
                system("clear");
                cout << "Starting Air Traffic Simulation..." << endl;
                cout << "Press 'q' at any time to return to the main menu." << endl;
                sleep(1);
                
                // Non-blocking input setup
                struct termios oldSettings, newSettings;
                tcgetattr(STDIN_FILENO, &oldSettings);
                newSettings = oldSettings;
                newSettings.c_lflag &= ~(ICANON | ECHO);
                tcsetattr(STDIN_FILENO, TCSANOW, &newSettings);
                
                fd_set readfds;
                struct timeval tv;
                bool simulationRunning = true;
                
                // Run the simulation loop
                while (simulationRunning && simulationTime < MAX_SIMULATION_TIME) {
                    // Update simulation
                    scheduler.updateSimulation();
                    
                    // Display status
                    //system("clear");
                    scheduler.printStatus();
                    cout << "\nSimulation Time: " << ++simulationTime << "/" << MAX_SIMULATION_TIME << " seconds" << endl;
                    cout << "Press 'q' to return to the main menu." << endl;
                    
                    // Check for user input (non-blocking)
                    FD_ZERO(&readfds);
                    FD_SET(STDIN_FILENO, &readfds);
                    tv.tv_sec = 0;
                    tv.tv_usec = 0;
                    
                    if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv)) {
                        char c = getchar();
                        if (c == 'q' || c == 'Q') {
                            simulationRunning = false;
                        }
                    }
                    
                    // Simulate 1 second passing
                    sleep(1);
                }
                
                // Restore terminal settings
                tcsetattr(STDIN_FILENO, TCSANOW, &oldSettings);
                
                if (simulationTime >= MAX_SIMULATION_TIME) {
                    cout << "\nSimulation completed!" << endl;
                    cout << "Press Enter to return to the main menu...";
                    cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                    cin.get();
                }
                break;
            }
            
            case 2: {
              
                
                // AVN Management Menu
                bool avnMenuActive = true;
                
                while (avnMenuActive) {
                    system("clear");
                    cout << "╔══════════════════════════════════════╗" << endl;
                    cout << "║          AVN MANAGEMENT              ║" << endl;
                    cout << "╠══════════════════════════════════════╣" << endl;
                    cout << "║ 1. View All Active AVNs              ║" << endl;
                    cout << "║ 2. View Airline-specific AVNs        ║" << endl;
                    cout << "║ 3. View AVN Details                  ║" << endl;
                    cout << "║ 4. Pay AVN                           ║" << endl;
                    cout << "║ 5. Return to Main Menu               ║" << endl;
                    cout << "╚══════════════════════════════════════╝" << endl;
                    cout << "Select an option: ";
                    
                    int avnChoice;
                    cin >> avnChoice;
                    
                    switch (avnChoice) {
                        case 1: {
                            system("clear");
                            cout << "\n--- ACTIVE AVNs ---" << endl;
                            const auto& allAVNs = scheduler.getAllAVNs();
                            if (allAVNs.empty()) {
                                cout << "No AVNs issued yet." << endl;
                            } else {
                                bool hasUnpaidAVNs = false;
                                for (const auto& avn : allAVNs) {
                                    if (avn->status == PaymentStatus::UNPAID) {
                                        cout << "AVN #" << avn->id << " | " << avn->airline << " flight " << avn->flightNumber 
                                            << " | Speed: " << avn->recordedSpeed << " km/h"
                                            << " | Amount: PKR " << fixed << setprecision(2) << avn->totalAmount << endl;
                                        hasUnpaidAVNs = true;
                                    }
                                }
                                
                                if (!hasUnpaidAVNs) {
                                    cout << "All AVNs have been paid." << endl;
                                }
                            }
                            cout << "\nPress Enter to continue...";
                            cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                            cin.get();
                            break;
                        }
                        
                        case 2: {
                            string airline;
                            cout << "Enter airline name: ";
                            cin >> airline;
                            
                            system("clear");
                            scheduler.displayAirlineViolations(airline);
                            cout << "\nPress Enter to continue...";
                            cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                            cin.get();
                            break;
                        }
                        
                        case 3: {
                            int avnId;
                            cout << "Enter AVN ID: ";
                            cin >> avnId;
                            
                            system("clear");
                            scheduler.displayAVNDetails(avnId);
                            cout << "\nPress Enter to continue...";
                            cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                            cin.get();
                            break;
                        }
                        
                        // Modify the case 4 in the AVN Management Menu section in main() around line 2190-2235

                        case 4: {
                            int avnId;
                            cout << "Enter AVN ID to pay: ";
                            cin >> avnId;
                            
                            // First, check if the AVN exists and get its required amount
                            const auto& allAVNs = scheduler.getAllAVNs();
                            bool avnFound = false;
                            
                            for (const auto& avn : allAVNs) {
                                if (avn->id == avnId) {
                                    avnFound = true;
                                    
                                    // Check if already paid
                                    if (avn->status == PaymentStatus::PAID) {
                                        system("clear");
                                        cout << "AVN #" << avnId << " has already been paid.\n";
                                        break;
                                    }
                                    
                                    // Display the AVN information before payment
                                    system("clear");
                                    cout << "=== AVN Payment ===\n";
                                    cout << "AVN #" << avn->id << " | " << avn->airline << " flight " << avn->flightNumber << "\n";
                                    cout << "Required amount: PKR " << fixed << setprecision(2) << avn->totalAmount << "\n\n";
                                    
                                    // Ask for confirmation
                                    char confirm;
                                    cout << "Do you want to pay this amount? (y/n): ";
                                    cin >> confirm;
                                    
                                    if (confirm == 'y' || confirm == 'Y') {
                                        // Process the payment with exact required amount
                                        scheduler.processAVNPayment(avnId, avn->totalAmount);
                                        cout << "\nPayment successful!\n";
                                    } else {
                                        cout << "\nPayment cancelled.\n";
                                    }
                                    break;
                                }
                            }
                            
                            if (!avnFound) {
                                system("clear");
                                cout << "AVN #" << avnId << " not found.\n";
                            }
                            
                            cout << "\nPress Enter to continue...";
                            cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                            cin.get();
                            break;
                        }
                        
                        case 5:
                            avnMenuActive = false;
                            break;
                            
                        default:
                            cout << "Invalid choice. Please try again." << endl;
                            sleep(1);
                            break;
                    }
                }
                break;
            }
            
            case 3: {
                // View Airline Violations (with a submenu)
                bool airlineMenuActive = true;
                
                while (airlineMenuActive) {
                    system("clear");
                    cout << "╔══════════════════════════════════════╗" << endl;
                    cout << "║        AIRLINE VIOLATIONS            ║" << endl;
                    cout << "╠══════════════════════════════════════╣" << endl;
                    cout << "║ 1. PIA                               ║" << endl;
                    cout << "║ 2. AirBlue                           ║" << endl;
                    cout << "║ 3. FedEx                             ║" << endl;
                    cout << "║ 4. Pakistan Airforce                 ║" << endl;
                    cout << "║ 5. Blue Dart                         ║" << endl;
                    cout << "║ 6. AghaKhan Air Ambulance            ║" << endl;
                    cout << "║ 7. Enter Custom Airline              ║" << endl;
                    cout << "║ 8. Return to Main Menu               ║" << endl;
                    cout << "╚══════════════════════════════════════╝" << endl;
                    cout << "Select an option: ";
                    
                    int airlineChoice;
                    cin >> airlineChoice;
                    
                    string selectedAirline = "";
                    
                    switch (airlineChoice) {
                        case 1:
                            selectedAirline = "PIA";
                            break;
                        case 2:
                            selectedAirline = "AirBlue";
                            break;
                        case 3:
                            selectedAirline = "FedEx";
                            break;
                        case 4:
                            selectedAirline = "Pakistan Airforce";
                            break;
                        case 5:
                            selectedAirline = "Blue Dart";
                            break;
                        case 6:
                            selectedAirline = "AghaKhan Air Ambulance";
                            break;
                        case 7:
                            cout << "Enter airline name: ";
                            cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                            getline(cin, selectedAirline);
                            break;
                        case 8:
                            airlineMenuActive = false;
                            continue;
                        default:
                            cout << "Invalid choice. Please try again." << endl;
                            sleep(1);
                            continue;
                    }
                    
                    if (!selectedAirline.empty()) {
                        system("clear");
                        scheduler.displayAirlineViolations(selectedAirline);
                        cout << "\nPress Enter to continue...";
                        cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                        cin.get();
                    }
                }
                break;
            }
            
            case 4:
                continueProgram = false;
                cout << "\nExiting AirControlX System. Goodbye!" << endl;
                break;
                
            default:
                cout << "Invalid choice. Please try again." << endl;
                sleep(1);
                break;
        }
    }
    
    // Clean up and wait for child processes
    if (avnPid > 0) {
        kill(avnPid, SIGTERM);
        waitpid(avnPid, nullptr, 0);
    }
    
    if (stripePid > 0) {
        kill(stripePid, SIGTERM);
        waitpid(stripePid, nullptr, 0);
    }
    
    if (airlinePid > 0) {
        kill(airlinePid, SIGTERM);
        waitpid(airlinePid, nullptr, 0);
    }
    
    return 0;
}