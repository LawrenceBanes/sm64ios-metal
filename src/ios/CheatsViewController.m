//
//  CheatsViewController.m
//  sm64ios
//
//  Created by Christian Kosman on 9/29/21.
//

#import "CheatsViewController.h"
#import "src/pc/cheats.h"

@implementation CheatsViewController

- (void)viewDidLoad {
    [super viewDidLoad];
#if TARGET_OS_IOS
    UIButton *playground = [UIButton buttonWithType:UIButtonTypeSystem];
    playground.frame = CGRectMake(0, 0, 300, 56);
    [playground setTitle:@"Open cheat playground" forState:UIControlStateNormal];
    [playground addTarget:self action:@selector(showPlayground) forControlEvents:UIControlEventTouchUpInside];
    self.tableView.tableHeaderView = playground;
#endif
}

#if TARGET_OS_IOS
- (void)showPlayground {
    [self presentViewController:ios_cheats_playground_controller() animated:YES completion:nil];
}

#endif
- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [self.enableCheatsSwitch setOn:Cheats.EnableCheats];
    [self.moonJumpSwitch setOn:Cheats.MoonJump];
    [self.godModeSwitch setOn:Cheats.GodMode];
    [self.infiniteLivesSwitch setOn:Cheats.InfiniteLives];
    [self.superSpeedSwitch setOn:Cheats.SuperSpeed];
    [self.responsiveSwitch setOn:Cheats.Responsive];
    [self.exitAnywhereSwitch setOn:Cheats.ExitAnywhere];
    [self.hugeMarioSwitch setOn:Cheats.HugeMario];
    [self.tinyMarioSwitch setOn:Cheats.TinyMario];
    
#if TARGET_OS_TV
    self.enableCheatsSwitch.callback = ^() {
        [self EnableCheatsChanged:nil];
    };
    self.moonJumpSwitch.callback = ^() {
        [self MoonJumpChanged:nil];
    };
    self.godModeSwitch.callback = ^() {
        [self GodModeChanged:nil];
    };
    self.infiniteLivesSwitch.callback = ^() {
        [self InfiniteLivesChanged:nil];
    };
    self.superSpeedSwitch.callback = ^() {
        [self SuperSpeedChanged:nil];
    };
    self.responsiveSwitch.callback = ^() {
        [self ResponsiveChanged:nil];
    };
    self.exitAnywhereSwitch.callback = ^() {
        [self ExitAnywhereChanged:nil];
    };
    self.hugeMarioSwitch.callback = ^() {
        [self HugeMarioChanged:nil];
    };
    self.tinyMarioSwitch.callback = ^() {
        [self TinyMarioChanged:nil];
    };
#endif
}

- (IBAction)EnableCheatsChanged:(id)sender {
    Cheats.EnableCheats = [self.enableCheatsSwitch isOn];
}

- (IBAction)MoonJumpChanged:(id)sender {
    Cheats.MoonJump = [self.moonJumpSwitch isOn];
}

- (IBAction)GodModeChanged:(id)sender {
    Cheats.GodMode = [self.godModeSwitch isOn];
}

- (IBAction)InfiniteLivesChanged:(id)sender {
    Cheats.InfiniteLives = [self.infiniteLivesSwitch isOn];
}

- (IBAction)SuperSpeedChanged:(id)sender {
    Cheats.SuperSpeed = [self.superSpeedSwitch isOn];
}

- (IBAction)ResponsiveChanged:(id)sender {
    Cheats.Responsive = [self.responsiveSwitch isOn];
}

- (IBAction)ExitAnywhereChanged:(id)sender {
    Cheats.ExitAnywhere = [self.exitAnywhereSwitch isOn];
}

- (IBAction)HugeMarioChanged:(id)sender {
    Cheats.HugeMario = [self.hugeMarioSwitch isOn];
}

- (IBAction)TinyMarioChanged:(id)sender {
    Cheats.TinyMario = [self.tinyMarioSwitch isOn];
}

- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
    [self.tableView deselectRowAtIndexPath:indexPath animated:true];
}

@end

#if TARGET_OS_IOS
@interface SM64PlaygroundViewController : UITableViewController
@end

@implementation SM64PlaygroundViewController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Cheats";
    self.tableView.rowHeight = 64;
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc]
        initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(close)];
}
- (void)close { [self dismissViewControllerAnimated:YES completion:nil]; }
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section { return 3; }
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    return @"Flight: stick steers, A rises, Z descends. Release the stick to coast. Bowser prototype uses native idle, walk and airborne animations, slower ground movement and Mario collision. Flight and Bowser suspend during warps, death, swimming and scripted actions. Settings last for this session.";
}
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    unsigned settings = cheats_playground_get();
    if (path.row == 0) {
        cell.textLabel.text = @"Mario’s hat";
        UISegmentedControl *hat = [[UISegmentedControl alloc] initWithItems:@[@"Normal", @"ON", @"OFF"]];
        hat.selectedSegmentIndex = settings & CHEAT_HAT_OFF ? 2 : settings & CHEAT_HAT_ON ? 1 : 0;
        hat.accessibilityLabel = @"Mario’s hat";
        [hat addTarget:self action:@selector(hatChanged:) forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = hat;
    } else {
        cell.textLabel.text = path.row == 1 ? @"Superman flight" : @"Bowser prototype";
        UISwitch *toggle = [[UISwitch alloc] init];
        toggle.tag = path.row == 1 ? CHEAT_FLIGHT : CHEAT_BOWSER;
        toggle.on = (settings & toggle.tag) != 0;
        toggle.accessibilityLabel = cell.textLabel.text;
        [toggle addTarget:self action:@selector(toggleChanged:) forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = toggle;
    }
    return cell;
}
- (void)hatChanged:(UISegmentedControl *)sender {
    unsigned value = sender.selectedSegmentIndex == 1 ? CHEAT_HAT_ON
                   : sender.selectedSegmentIndex == 2 ? CHEAT_HAT_OFF : 0;
    cheats_playground_set(CHEAT_HAT_ON | CHEAT_HAT_OFF, value);
}
- (void)toggleChanged:(UISwitch *)sender {
    cheats_playground_set((unsigned)sender.tag, sender.isOn ? (unsigned)sender.tag : 0);
}
@end

UIViewController *ios_cheats_playground_controller(void) {
    return [[UINavigationController alloc] initWithRootViewController:
        [[SM64PlaygroundViewController alloc] initWithStyle:UITableViewStyleInsetGrouped]];
}

#endif
